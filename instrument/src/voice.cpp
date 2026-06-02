#include "instrument/voice.hpp"

#include "idsp/functions.hpp"
#include "idsp/lookup.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kHalfPi = 1.57079632679489661923f;

    // Pan LUT generator: holds cos(π/2 · t) on t∈[0,1].
    // L-gain is read at pan, R-gain at (1 − pan) — same curve, mirrored input.
    float pan_curve(float t) { return std::cos(kHalfPi * t); }

    const idsp::LookupTable<float, 257>& pan_lut()
    {
        static const idsp::LookupTable<float, 257> lut(&pan_curve);
        return lut;
    }

    constexpr float kPi = 3.14159265358979323846f;

    // --- Grain window LUTs ---
    //
    // 7 precomputed windows. Indices 0-1 are non-Kaiser shapes prepended for
    // creative use; indices 2-6 are Kaiser windows at β ∈ {0, 3, 6, 10, 14}.
    //
    //   0: rectangular  — constant 1  (hard, no taper)
    //   1: downramp     — linear 1→0  (attack-biased, asymmetric)
    //   2: Kaiser β=0   — constant 1  (same as rect; COLA anchor)
    //   3: Kaiser β=3   — mild Hamming-like taper
    //   4: Kaiser β=6   — Hann-equivalent (canonical C-OLA)
    //   5: Kaiser β=10  — strong taper
    //   6: Kaiser β=14  — very smooth / minimal sidelobes
    //
    // shape_deviation_ ∈ [-1, 1] maps linearly to a float index in [0, 6]:
    //   -1 → 0 (rectangular),  0 → 3 (Kaiser β=3),  +1 → 6 (Kaiser β=14).

    constexpr int    kKaiserLutSize = 1024;
    constexpr size_t kKaiserCount   = 5;
    constexpr size_t kWindowCount   = kKaiserCount + 2;  // rect + downramp + 5 Kaiser
    constexpr float  kKaiserBetas[kKaiserCount] = { 0.f, 3.f, 6.f, 10.f, 14.f };

    // Modified Bessel I0 via the canonical power-series expansion. Converges
    // in ~20 terms for x ≤ 14; used only at LUT-generation time.
    inline double bessel_i0(double x)
    {
        const double y = x * 0.5;
        double term = 1.0;
        double sum  = 1.0;
        for (int k = 1; k < 32; ++k)
        {
            term *= (y / static_cast<double>(k));
            const double t2 = term * term;
            sum += t2;
            if (t2 < 1e-18 * sum) break;
        }
        return sum;
    }

    inline double kaiser_value(double phase, double beta)
    {
        // Standard Kaiser: w(n) = I0(β·√(1 − (2n/(N-1) − 1)²)) / I0(β)
        if (beta <= 0.0) return 1.0;
        const double x = 2.0 * phase - 1.0;          // [-1, +1]
        const double r2 = 1.0 - x * x;
        if (r2 < 0.0) return 0.0;
        return bessel_i0(beta * std::sqrt(r2)) / bessel_i0(beta);
    }

    using GrainWindowLut = idsp::LookupTable<float, kKaiserLutSize>;

    inline std::array<GrainWindowLut, kWindowCount> build_window_luts()
    {
        std::array<GrainWindowLut, kWindowCount> tables{
            GrainWindowLut{[](float)   { return 1.f;       }},          // 0: rectangular
            GrainWindowLut{[](float t) { return 1.f - t;   }},          // 1: downramp
            GrainWindowLut{[](float t) { return static_cast<float>(kaiser_value(t,  0.0)); }},  // 2: Kaiser β=0
            GrainWindowLut{[](float t) { return static_cast<float>(kaiser_value(t,  3.0)); }},  // 3: Kaiser β=3
            GrainWindowLut{[](float t) { return static_cast<float>(kaiser_value(t,  6.0)); }},  // 4: Kaiser β=6
            GrainWindowLut{[](float t) { return static_cast<float>(kaiser_value(t, 10.0)); }},  // 5: Kaiser β=10
            GrainWindowLut{[](float t) { return static_cast<float>(kaiser_value(t, 14.0)); }},  // 6: Kaiser β=14
        };
        return tables;
    }

    const std::array<GrainWindowLut, kWindowCount>& window_luts()
    {
        static const std::array<GrainWindowLut, kWindowCount> tables = build_window_luts();
        return tables;
    }

    // Select two adjacent window LUT indices from a float index in [0, kWindowCount-1].
    inline void select_window_luts(float index, int& a, int& b, float& blend)
    {
        const float clamped = idsp::clamp(index, 0.f, static_cast<float>(kWindowCount - 1));
        a     = static_cast<int>(clamped);
        b     = std::min(a + 1, static_cast<int>(kWindowCount) - 1);
        blend = clamped - static_cast<float>(a);
    }

    // Read the blended window at `phase` ∈ [0, 1].
    inline float read_window(int a, int b, float blend, float phase)
    {
        const auto& luts = window_luts();
        const float pc = idsp::clamp(phase, 0.f, 1.f);
        const float va = luts[a].read(pc);
        const float vb = luts[b].read(pc);
        return (1.f - blend) * va + blend * vb;
    }

    // Minimum overlap count for C-OLA reconstruction at the given window index.
    // rect/downramp need 1-2; Hann-like (index 4) needs 2; high-β Kaisers need 3-4.
    inline int cola_overlap_for_window(float index)
    {
        constexpr int overlaps[kWindowCount] = { 1, 2, 1, 2, 2, 3, 4 };
        const int i = idsp::clamp(static_cast<int>(std::round(index)),
                                  0, static_cast<int>(kWindowCount) - 1);
        return overlaps[i];
    }

    // xorshift32 — local rng for per-grain jitter.
    inline uint32_t xorshift32(uint32_t& s)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }
    inline float bipolar_uniform(uint32_t& s)
    {
        return static_cast<float>(xorshift32(s) >> 8) * (1.f / 8388608.f) - 1.f;
    }

    // Per-grain jitter curve. depth ∈ [0,1] controls the magnitude:
    //   0.0  → 0
    //   0.5  → small structured decorrelation
    //   1.0  → full spray
    // Curve = depth × structured + depth^4 × (spray − structured), uniformly
    // distributed in ±amount.
    inline float per_grain_jitter(uint32_t& rng, float depth, float spray_range)
    {
        const float d = idsp::clamp(depth, 0.f, 1.f);
        constexpr float kStructured = 0.05f;
        const float structured_amt = d * kStructured * spray_range;
        const float spray_amt      = d * d * d * d * (1.f - kStructured) * spray_range;
        const float amount = structured_amt + spray_amt;
        return bipolar_uniform(rng) * amount;
    }

    // Equal-power pan via the shared LUT. ~6 fmadds, no sqrt/sin/cos at runtime.
    inline void compute_pan_gains_lut(float pan, float& l, float& r)
    {
        const auto& lut = pan_lut();

        const float fi_l = pan * 256.f;
        int i_l = static_cast<int>(fi_l);
        if (i_l < 0)    i_l = 0;
        if (i_l >= 256) i_l = 255;
        const float fr_l = fi_l - static_cast<float>(i_l);
        l = lut.read(i_l, fr_l);

        const float fi_r = (1.f - pan) * 256.f;
        int i_r = static_cast<int>(fi_r);
        if (i_r < 0)    i_r = 0;
        if (i_r >= 256) i_r = 255;
        const float fr_r = fi_r - static_cast<float>(i_r);
        r = lut.read(i_r, fr_r);
    }
}

void Voice::set_live_params(const VoiceLiveParams& p, size_t buffer_size, float sample_rate)
{
    // Start / length / end derive from sliders + current buffer size.
    const float start_clamped  = idsp::clamp(p.start,  0.f, 1.f);
    const float length_clamped = idsp::clamp(p.length, 0.f, 1.f);
    const size_t start_pos = static_cast<size_t>(start_clamped * static_cast<float>(buffer_size));
    const size_t loop_len  = idsp::max<size_t>(
        static_cast<size_t>(length_clamped * static_cast<float>(buffer_size)), 1);
    const size_t end_pos   = idsp::min(start_pos + loop_len, buffer_size);

    start_pos_ = start_pos;
    end_pos_   = end_pos;
    length_    = loop_len;

    // Speed = slider value × (note-ratio multiplier when timestretch is OFF,
    // 1× otherwise). The note-ratio multiplier folds the MIDI per-note pitch
    // shift into the playback rate for non-timestretch voices; for timestretch
    // voices the note pitch is applied to pitch_deviation below instead.
    // `forward_` follows the live sign.
    const float speed_input = p.speed * (p.timestretch ? 1.f
                                                       : std::exp2(midi_octave_offset_));
    forward_    = (speed_input >= 0.f);
    base_speed_ = speed_input;

    base_level_ = p.level;
    base_pan_   = p.pan;
    compute_pan_gains_lut(p.pan, base_pan_l_, base_pan_r_);
    // sample_loops_ is intrinsic to the trigger type now (play/MIDI → true,
    // envelope_trigger AR → false); each trigger_* sets it before calling
    // this function. Don't overwrite it here.

    if (p.scale_envelope)
    {
        // decay slider is the master duration: total = decay × loop_duration.
        // attack / release sliders are fractions of that total; hold fills the rest.
        // `length_` was just computed above; speed_abs is the position traversal
        // rate (|p.speed| — excludes MIDI pitch offset, which is exact for the
        // beat-slicer Position routing use case).
        const float speed_abs = std::max(std::abs(p.speed), 0.001f);
        const float loop_dur  = static_cast<float>(length_) / speed_abs;
        const float total_dur = idsp::clamp(p.decay, 0.f, 1.f) * loop_dur;
        const float a_frac    = idsp::clamp(p.attack,  0.f, 1.f);
        const float r_frac    = idsp::clamp(p.release, 0.f, 1.f);
        const float h_frac    = idsp::clamp(1.f - a_frac - r_frac, 0.f, 1.f);

        env_attack_  = static_cast<size_t>(a_frac * total_dur);
        env_hold_    = static_cast<size_t>(h_frac * total_dur);
        env_release_ = static_cast<size_t>(r_frac * total_dur);
        env_decay_   = 0;
    }
    else
    {
        // Fixed 0..5 s mapping, independent of loop region.
        const auto resolve_dur = [&](float slider_value) -> size_t
        {
            const float scale = 5.f * sample_rate;
            const auto raw = static_cast<size_t>(idsp::clamp(slider_value, 0.f, 1.f) * scale);
            return raw == 0 ? 1 : raw;
        };

        env_attack_  = resolve_dur(p.attack);
        env_decay_   = resolve_dur(p.decay);
        env_hold_    = 0;
        env_release_ = resolve_dur(p.release);
    }
    env_sustain_level_ = idsp::clamp(p.sustain, 0.f, 1.f);

    filter_cutoff_ = p.filter_freq * p.sample_rate_inv;
    filter_resonance_ = p.filter_q;

    depth_speed_  = p.envelope_speed;
    depth_start_  = p.envelope_start;
    depth_length_ = p.envelope_length;
    depth_level_  = p.envelope_level;
    depth_pan_    = p.envelope_pan;
    depth_cutoff_ = p.envelope_cutoff;
    depth_resonance_ = p.envelope_resonance;

    depth_phase_speed_  = p.phase_speed;
    depth_phase_start_  = p.phase_start;
    depth_phase_length_ = p.phase_length;
    depth_phase_level_  = p.phase_level;
    depth_phase_pan_    = p.phase_pan;

    // --- Granular: copy deviations + random depths, then derive auto values ---
    pitch_deviation_   = idsp::clamp(p.pitch_deviation,  -2.f, 2.f);
    size_deviation_    = idsp::clamp(p.size_deviation,   -1.f, 1.f);
    shape_deviation_   = idsp::clamp(p.shape_deviation,  -1.f, 1.f);
    grains_deviation_  = idsp::clamp(p.grains_deviation, -1.f, 1.f);
    timestretch_       = p.timestretch;
    random_pitch_      = idsp::clamp(p.random_pitch,      0.f, 1.f);
    random_size_       = idsp::clamp(p.random_size,       0.f, 1.f);
    random_shape_      = idsp::clamp(p.random_shape,      0.f, 1.f);
    random_grains_     = idsp::clamp(p.random_grains,     0.f, 1.f);
    random_position_   = idsp::clamp(p.random_position,   0.f, 1.f);
    random_level_      = idsp::clamp(p.random_level,      0.f, 1.f);
    random_pan_        = idsp::clamp(p.random_pan,        0.f, 1.f);
    sample_rate_       = sample_rate;

    // 1. Pitch ratio. When timestretch is OFF, pitch is forced to 1 (no shift)
    //    and the voice plays as a single playhead (loop_crossfade_mode_ below).
    //    When ON, the slider's pitch_deviation sums with the MIDI per-note
    //    pitch offset stored on this voice. `forward_` carries playback direction.
    const float effective_pitch_oct = timestretch_
        ? (pitch_deviation_ + midi_octave_offset_)
        : 0.f;
    const float pitch_mag = std::exp2(effective_pitch_oct);
    pitch_ratio_ = forward_ ? pitch_mag : -pitch_mag;

    // 2. Auto grain length in samples:
    //      N_auto_s = 40 ms baseline
    //               + 30 ms per octave of pitch shift
    //               + 20 ms when speed drops below 1× (slow playback smears most)
    //    size_deviation log-scales it: ±1 ⇒ ×0.25..×4, clamped to [20, 200] ms.
    const float n_auto_s = 0.040f
        + 0.030f * std::abs(effective_pitch_oct)
        + 0.020f * std::max(0.f, 1.f - std::abs(base_speed_));
    const float size_scale = std::exp2(size_deviation_ * 2.f);
    const float n_eff_s    = idsp::clamp(n_auto_s * size_scale, 0.020f, 0.200f);
    n_eff_samples_ = std::max(1.f, n_eff_s * sample_rate_);

    // 3. Window index from shape_deviation: linearly maps [-1, 1] → [0, kWindowCount-1].
    //    Index 0 = rectangular, 1 = downramp, 2-6 = Kaiser β ∈ {0, 3, 6, 10, 14}.
    window_index_ = (shape_deviation_ + 1.f) * 0.5f * static_cast<float>(kWindowCount - 1);
    window_index_ = idsp::clamp(window_index_, 0.f, static_cast<float>(kWindowCount - 1));

    // 4. Grain count from grains_deviation. -1 → 1 grain, 0 → C-OLA minimum
    //    for the chosen window (1/2/3/4), +1 → 8 grains. Piecewise linear.
    const int auto_overlap = cola_overlap_for_window(window_index_);
    int eff;
    if (grains_deviation_ <= 0.f)
    {
        const float t = grains_deviation_ + 1.f;  // [0, 1]
        eff = static_cast<int>(std::lround(1.f + t * static_cast<float>(auto_overlap - 1)));
    }
    else
    {
        eff = static_cast<int>(std::lround(
            static_cast<float>(auto_overlap)
                + grains_deviation_ * static_cast<float>(8 - auto_overlap)));
    }
    overlap_eff_ = idsp::clamp(eff, 1, static_cast<int>(grains_.size()));

    // 5. Loop-crossfade mode is now selected by timestretch=false (single
    //    playhead at original pitch). Granular C-OLA only runs when
    //    timestretch is ON.
    loop_crossfade_mode_ = !timestretch_;


}

float Voice::loop_position_fraction() const
{
    if (length_ == 0) return 0.f;
    const float rel = (position_ - static_cast<float>(start_pos_))
                    / static_cast<float>(length_);
    return idsp::clamp(rel, 0.f, 1.f);
}

void Voice::set_loop_position_fraction(float frac)
{
    if (length_ == 0) return;
    const float f = idsp::clamp(frac, 0.f, 1.f);
    position_ = static_cast<float>(start_pos_) + f * static_cast<float>(length_);

    if (loop_crossfade_mode_)
    {
        // Single-playhead mode: teleport the Body grain's read pointer so the
        // jump is immediate. (There's no overlap to mask the change.) If a
        // boundary crossfade is in progress, leave the FadeIn/FadeOut pair
        // alone — they'll resolve normally.
        for (auto& g : grains_)
        {
            if (g.active && g.role == Grain::Role::Body) g.read_pos = position_;
        }
    }
    // C-OLA mode: update `position_` and otherwise leave the cluster running.
    // Existing grains finish their windows from the old read positions; new
    // grains spawn at the updated `position_` on the next synth-hop. The
    // overlapping windows produce a smooth crossfade between the old and
    // scrubbed positions instead of cutting on every block during a continuous
    // drag (the old behaviour of `reset_grains()` here killed the cluster
    // every block, producing the "stuttering grains while scrubbing, then a
    // burst on release" artifact).
}

// Lifetime progress in [0, 1]: 0 just after trigger, 1 about to hit the end.
// Direction-aware so reverse playback ramps the same way forward does.
float Voice::phase() const
{
    if (length_ == 0) return 0.f;
    const float len = static_cast<float>(length_);
    const float rel = forward_
        ? (position_ - static_cast<float>(start_pos_)) / len
        : (static_cast<float>(end_pos_) - position_)  / len;
    return idsp::clamp(rel, 0.f, 1.f);
}

float Voice::current_level() const
{
    if (!active_) return 0.f;
    return (envelope_mode_ == EnvelopeMode::None)
        ? base_level_
        : envelope_.current_value() * base_level_;
}

void Voice::reset_grains()
{
    for (auto& g : grains_) g = Grain{};
}

void Voice::spawn_grain_loop_xfade(size_t slot, Grain::Role role, float read_pos, float phase_inc)
{
    if (slot >= grains_.size()) return;
    auto& g = grains_[slot];
    g.active     = true;
    g.role       = role;
    g.read_pos   = read_pos;
    g.pitch_ratio = forward_ ? std::abs(pitch_ratio_) : -std::abs(pitch_ratio_);
    g.phase      = 0.f;
    g.phase_inc  = phase_inc;
    g.win_lut_a  = 0;
    g.win_lut_b  = 0;
    g.win_blend  = 0.f;
}

void Voice::spawn_cola_grain(size_t slot, float base_read_pos)
{
    if (slot >= grains_.size()) return;
    auto& g = grains_[slot];
    g.active = true;
    g.role   = Grain::Role::Body;
    g.phase  = 0.f;

    // Per-grain position jitter — spray range = one grain length, so at
    // depth=1 each grain can start anywhere within ±N samples of the
    // analysis pointer; at depth=0.5 it's a few-percent decorrelation.
    // Result is wrapped into the loop region by wrap_position.
    const float pos_jit = per_grain_jitter(grain_rng_, random_position_, n_eff_samples_);
    const float start_f = static_cast<float>(start_pos_);
    const float end_f   = static_cast<float>(end_pos_);
    g.read_pos    = wrap_position(base_read_pos + pos_jit, start_f, end_f);
    g.spawn_start = start_f;
    g.spawn_end   = end_f;

    // Per-grain pitch jitter, up to ±1 octave at depth=1. The voice's
    // `pitch_ratio_` already incorporates both the slider pitch_deviation
    // and the MIDI per-note octave offset (computed in set_live_params), so
    // we multiply that base by the jitter to get the per-grain rate.
    const float pitch_jit = per_grain_jitter(grain_rng_, random_pitch_, 1.f);
    const float p_ratio   = std::abs(pitch_ratio_) * std::exp2(pitch_jit);
    g.pitch_ratio = forward_ ? p_ratio : -p_ratio;

    // Per-grain size jitter scales N: ±1 in exp2 → ×0.5..×2 at depth=1.
    const float size_jit   = per_grain_jitter(grain_rng_, random_size_, 1.f);
    const float n_samples  = std::max(1.f, n_eff_samples_ * std::exp2(size_jit));
    g.phase_inc = 1.f / n_samples;

    // Per-grain shape jitter in window-index space. Spray range = full span so
    // at depth=1 any window can be reached from any base index.
    const float shape_jit   = per_grain_jitter(grain_rng_, random_shape_,
                                               static_cast<float>(kWindowCount - 1));
    const float index_local = idsp::clamp(window_index_ + shape_jit,
                                          0.f, static_cast<float>(kWindowCount - 1));
    select_window_luts(index_local, g.win_lut_a, g.win_lut_b, g.win_blend);

    // Per-grain level and pan jitter. Both are additive offsets to the
    // voice's base_level_ / base_pan_, applied per-grain inside process()
    // and then clamped into [0, 1]. Spray range = 1.0 so at depth=1 the
    // jitter can swing the full slider range.
    g.level_jit = per_grain_jitter(grain_rng_, random_level_, 1.f);
    g.pan_jit   = per_grain_jitter(grain_rng_, random_pan_,   1.f);
}

size_t Voice::find_grain_slot()
{
    // Prefer any inactive slot.
    for (size_t i = 0; i < grains_.size(); ++i)
    {
        if (!grains_[i].active) return i;
    }
    // Otherwise steal the slot with highest phase (oldest grain — closest to dying).
    size_t oldest = 0;
    float max_phase = grains_[0].phase;
    for (size_t i = 1; i < grains_.size(); ++i)
    {
        if (grains_[i].phase > max_phase) { max_phase = grains_[i].phase; oldest = i; }
    }
    return oldest;
}

float Voice::wrap_position(float pos, float start_f, float end_f) const
{
    const float duration_f = end_f - start_f;
    if (duration_f <= 0.f) return pos;
    if (sample_loops_)
    {
        pos -= start_f;
        pos -= duration_f * std::floor(pos / duration_f);
        pos += start_f;
    }
    return pos;
}

void Voice::wrap_grain_read(Grain& g, float start_f, float end_f) const
{
    const float duration_f = end_f - start_f;
    if (duration_f <= 0.f) return;
    if (g.read_pos >= end_f || g.read_pos < start_f)
    {
        if (sample_loops_)
        {
            g.read_pos -= start_f;
            g.read_pos -= duration_f * std::floor(g.read_pos / duration_f);
            g.read_pos += start_f;
            // Guard against fp rounding landing exactly on end_f.
            if (g.read_pos >= end_f) g.read_pos = end_f - 1.f;
        }
        else { g.active = false; }
    }
}

void Voice::prepare_for_trigger(const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq)
{
    forward_ = (p.speed >= 0.f);
    this->set_live_params(p, buffer_size, sample_rate);
    this->retrigger_position();

    // Reset the grain cluster and seed the synth-side spawning state.
    this->reset_grains();
    synth_hop_counter_         = 0.f;          // spawn immediately on first sample
    last_loop_crossfade_mode_  = loop_crossfade_mode_;

    if (loop_crossfade_mode_)
    {
        // Body grain: phase doesn't advance; amp stays at 1 until crossfade.
        this->spawn_grain_loop_xfade(0, Grain::Role::Body, position_, 0.f);
    }
    // C-OLA mode: the first process() sample will fire a spawn via the
    // hop counter at zero, so nothing to do here.

    active_     = (length_ > 0);
    launch_seq_ = seq;
}

void Voice::trigger_plain(const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq)
{
    sample_loops_ = true;   // play voices always loop
    // Non-MIDI launch: clear any inherited MIDI offsets so a recycled slot
    // doesn't carry pitch / velocity scaling from a previous note.
    midi_octave_offset_   = 0.f;
    midi_velocity_factor_ = 1.f;
    this->prepare_for_trigger(p, buffer_size, sample_rate, seq);
    envelope_mode_ = EnvelopeMode::None;
    envelope_.reset();
}

void Voice::trigger_ar(const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq)
{
    sample_loops_ = false;  // envelope_trigger AR voices are one-shot
    midi_octave_offset_   = 0.f;
    midi_velocity_factor_ = 1.f;
    this->prepare_for_trigger(p, buffer_size, sample_rate, seq);
    envelope_mode_ = EnvelopeMode::AR;
    envelope_.trigger_ar(env_attack_, env_release_);
}

void Voice::trigger_adsr_gated(const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq)
{
    sample_loops_ = true;   // MIDI voices loop while gated
    // Reset to identity first; Instrument calls set_midi_offsets right after
    // to install the real per-note values.
    midi_octave_offset_   = 0.f;
    midi_velocity_factor_ = 1.f;
    this->prepare_for_trigger(p, buffer_size, sample_rate, seq);
    envelope_mode_ = EnvelopeMode::ADSR;
    envelope_.trigger_adsr(env_attack_, env_decay_, env_sustain_level_, env_release_);
}

void Voice::trigger_ahr(const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq)
{
    sample_loops_         = false;  // one-shot: self-terminates when envelope ends
    midi_octave_offset_   = 0.f;
    midi_velocity_factor_ = 1.f;
    this->prepare_for_trigger(p, buffer_size, sample_rate, seq);
    envelope_mode_ = EnvelopeMode::AR;
    envelope_.trigger_ahr(env_attack_, env_hold_, env_sustain_level_, env_release_);
}

void Voice::trigger_ahsr_gated(const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq)
{
    sample_loops_         = true;
    midi_octave_offset_   = 0.f;
    midi_velocity_factor_ = 1.f;
    this->prepare_for_trigger(p, buffer_size, sample_rate, seq);
    envelope_mode_ = EnvelopeMode::ADSR;
    envelope_.trigger_ahsr_gated(env_attack_, env_hold_, env_sustain_level_, env_release_);
}

void Voice::set_midi_offsets(float octave_offset, float velocity_factor)
{
    midi_octave_offset_   = octave_offset;
    midi_velocity_factor_ = velocity_factor;
}

void Voice::kill()
{
    active_ = false;
    envelope_.reset();
}

void Voice::release()
{
    // AR covers scale_envelope MIDI voices (self-terminating AHR but note-off
    // can still cut short the hold/attack and jump straight to release).
    if (envelope_mode_ == EnvelopeMode::ADSR || envelope_mode_ == EnvelopeMode::AR)
        envelope_.release();
}

void Voice::retrigger_position()
{
    position_ = forward_ ? static_cast<float>(start_pos_)
                         : static_cast<float>(end_pos_ > 0 ? end_pos_ - 1 : 0);
}

bool Voice::check_bounds(float effective_end)
{
    const float start_f    = static_cast<float>(start_pos_);
    const float duration_f = effective_end - start_f;
    if (duration_f <= 0.f) { active_ = false; return false; }

    if (position_ >= effective_end || position_ < start_f)
    {
        if (sample_loops_)
        {
            position_ -= start_f;
            position_ -= duration_f * std::floor(position_ / duration_f);
            position_ += start_f;
            // Guard against fp rounding landing exactly on effective_end.
            if (position_ >= effective_end) position_ = effective_end - 1.f;
        }
        else { active_ = false; return false; }
    }
    return true;
}

Voice::StereoFrame Voice::process(const idsp::LagrangeDelay<524288>& left,
                                  const idsp::LagrangeDelay<524288>& right)
{
    if (!active_) return {0.f, 0.f};

    // --- Envelope step (None voices keep envelope idle → e stays 0).
    const bool was_active = envelope_.is_active();
    const float e = (envelope_mode_ == EnvelopeMode::None)
        ? 0.f
        : envelope_.process();
    const bool now_active = envelope_.is_active();

    // Per-voice playhead phase in [0, 1] — used as a second modulator
    // alongside the envelope. Computed from the pre-advance position so the
    // modulation it drives matches the sample we're about to read.
    const float ph = this->phase();

    // --- Speed (magnitude only; direction locked at trigger) ---
    float speed = base_speed_;
    if (depth_speed_ != 0.f || depth_phase_speed_ != 0.f)
    {
        const float base_mag = forward_ ? base_speed_ : -base_speed_;
        float mag = base_mag + (depth_speed_ * e + depth_phase_speed_ * ph) * 4.f;
        if (mag < 0.f) mag = 0.f;
        speed = forward_ ? mag : -mag;
    }

    // --- Start (read-position offset, applied to every grain) ---
    const float start_mod  = depth_start_ * e + depth_phase_start_ * ph;
    const float read_offset = start_mod * static_cast<float>(length_);

    // --- Length (dynamic effective end, also bounds the grain wraps) ---
    const float length_mod   = depth_length_ * e + depth_phase_length_ * ph;
    const float effective_end = (length_mod != 0.f)
        ? static_cast<float>(end_pos_) + length_mod * static_cast<float>(length_)
        : static_cast<float>(end_pos_);

    // --- Cutoff / Resonance modulation (bipolar [-1, +1]) ---
    float cutoff_mod;
    if(depth_cutoff_ >= 0.f) cutoff_mod = (1.f - depth_cutoff_) + depth_cutoff_ * e;
    else                     cutoff_mod = 1.f + depth_cutoff_ * (1.f - e);

    float resonance_mod;
    if(depth_resonance_ >= 0.f) resonance_mod = (1.f - depth_resonance_) + depth_resonance_ * e;
    else                         resonance_mod = 1.f + depth_resonance_ * (1.f - e);

    // Clamp before SVFilter: tan(π·f) blows up at f≥0.5; k=1/Q blows up at Q=0.
    const float safe_cutoff = idsp::clamp(cutoff_mod * filter_cutoff_, 0.f, 0.499f);
    const float safe_q      = idsp::max(resonance_mod * filter_resonance_, 0.1f);
    filter_l.set_parameters(safe_cutoff, safe_q);
    filter_r.set_parameters(safe_cutoff, safe_q);

    // --- Level / Pan modulation factors (uniform across grains) ---
    // The env/phase mod chain is uniform across all grains active this
    // sample; the grain-specific level/pan jitter is added inside the grain
    // loop. depth_level_ acts as wet/dry between "ignore envelope" (d=0 →
    // factor=1) and "envelope is the VCA" (d=±1 → factor=e or 1-e). For
    // None-mode voices we skip envelope_level entirely.
    float env_level_mod;
    if (depth_level_ >= 0.f) env_level_mod = (1.f - depth_level_) + depth_level_ * e;
    else                     env_level_mod = 1.f + depth_level_ * (1.f - e);

    float phase_level_mod;
    if (depth_phase_level_ >= 0.f) phase_level_mod = (1.f - depth_phase_level_) + depth_phase_level_ * ph;
    else                           phase_level_mod = 1.f + depth_phase_level_ * (1.f - ph);

    const float env_phase_level_scale = (envelope_mode_ == EnvelopeMode::None)
        ? phase_level_mod
        : env_level_mod * phase_level_mod;

    // Additive pan offset from envelope + phase modulation; per-grain jitter
    // is added on top of this inside the grain loop.
    const float pan_env_phase_mod = depth_pan_ * e + depth_phase_pan_ * ph;

    // --- Granular cluster read & advance ---
    const float start_f = static_cast<float>(start_pos_);
    const float end_f   = effective_end;

    // Detect a flip between loop-crossfade and C-OLA modes — reset cluster.
    if (loop_crossfade_mode_ != last_loop_crossfade_mode_)
    {
        this->reset_grains();
        if (loop_crossfade_mode_)
        {
            this->spawn_grain_loop_xfade(0, Grain::Role::Body, position_, 0.f);
        }
        else
        {
            synth_hop_counter_ = 0.f;       // spawn immediately
        }
        last_loop_crossfade_mode_ = loop_crossfade_mode_;
    }

    float acc_l = 0.f, acc_r = 0.f;

    if (loop_crossfade_mode_)
    {
        // --- Single-playhead mode with loop-boundary crossfade. ---
        // Crossfade duration = half the effective window length (in seconds:
        // half of n_eff_samples_).
        const float half_window_samples = 0.5f * n_eff_samples_;

        if (sample_loops_ && grains_[0].active && grains_[0].role == Grain::Role::Body
            && !grains_[1].active && shape_deviation_ > -0.99f)
        {
            const float dist = forward_ ? (end_f - position_) : (position_ - start_f);
            if (dist > 0.f && dist <= half_window_samples)
            {
                grains_[0].role      = Grain::Role::FadeOut;
                grains_[0].phase     = 0.f;
                grains_[0].phase_inc = 1.f / half_window_samples;
                this->spawn_grain_loop_xfade(
                    1, Grain::Role::FadeIn,
                    forward_ ? start_f : (end_f - 1.f),
                    1.f / half_window_samples);
            }
        }

        // Crossfade ramp curve at shape=-1 → hard step; otherwise linear.
        auto ramp = [](float shape_dev, float t) -> float
        {
            if (shape_dev <= -0.99f) return (t >= 0.5f) ? 1.f : 0.f;
            // shape_dev ∈ (-0.99, 1] → blend linear → raised cosine
            const float blend  = idsp::clamp((shape_dev + 1.f) * 0.5f, 0.f, 1.f);
            const float linear = t;
            const float cosw   = 0.5f - 0.5f * std::cos(kPi * t);
            return (1.f - blend) * linear + blend * cosw;
        };

        for (size_t gi = 0; gi < 2; ++gi)
        {
            auto& g = grains_[gi];
            if (!g.active) continue;

            float amp;
            if      (g.role == Grain::Role::FadeOut) amp = ramp(shape_deviation_, 1.f - g.phase);
            else if (g.role == Grain::Role::FadeIn)  amp = ramp(shape_deviation_, g.phase);
            else                                     amp = 1.f;

            // Per-grain level + pan. In loop-crossfade mode level_jit/pan_jit
            // are 0 (spawn_grain_loop_xfade doesn't apply random), so this is
            // equivalent to a uniform base_level/base_pan with env/phase mods.
            const float g_level = idsp::clamp(base_level_ + g.level_jit, 0.f, 1.f)
                                * env_phase_level_scale * midi_velocity_factor_;
            const float g_pan   = idsp::clamp(base_pan_ + pan_env_phase_mod + g.pan_jit, 0.f, 1.f);
            float g_pan_l, g_pan_r;
            compute_pan_gains_lut(g_pan, g_pan_l, g_pan_r);

            const float rp = g.read_pos + read_offset;

            acc_l += amp * g_level * g_pan_l * left.read_at(rp);
            acc_r += amp * g_level * g_pan_r * right.read_at(rp);

            filter_l.process(acc_l);
            filter_r.process(acc_r);

            acc_l = filter_l.get_lowpass();
            acc_r = filter_r.get_lowpass();

            // Loop-crossfade mode is the "single playhead" case — read rate
            // follows live `speed`, not a snapshot taken at spawn time. This
            // makes speed-slider edits take effect immediately rather than at
            // the next loop seam.
            g.read_pos += speed;

            if (g.role == Grain::Role::Body)
            {
                // Don't hard-wrap the Body grain at loop boundaries — trigger a
                // crossfade instead. This covers both the natural loop-end case
                // (backup to the proactive dist check above) and the start/length
                // scrub case where the boundary moved past the grain mid-playback.
                // Without this, a hard wrap here produces an audible click.
                if (g.read_pos >= end_f || g.read_pos < start_f)
                {
                    if (sample_loops_ && !grains_[1].active && shape_deviation_ > -0.99f)
                    {
                        g.role      = Grain::Role::FadeOut;
                        g.phase     = 0.f;
                        g.phase_inc = 1.f / half_window_samples;
                        this->spawn_grain_loop_xfade(
                            1, Grain::Role::FadeIn,
                            forward_ ? start_f : (end_f - 1.f),
                            1.f / half_window_samples);
                    }
                    else
                    {
                        // shape=-1 (hard step) or crossfade already running:
                        // fall back to the hard wrap.
                        wrap_grain_read(g, start_f, end_f);
                    }
                }
            }
            else
            {
                wrap_grain_read(g, start_f, end_f);
            }

            if (g.role != Grain::Role::Body)
            {
                g.phase += g.phase_inc;
                if (g.phase >= 1.f && g.role == Grain::Role::FadeOut)
                {
                    g.active = false;
                    if (grains_[1].active)
                    {
                        grains_[0]           = grains_[1];
                        grains_[0].role      = Grain::Role::Body;
                        grains_[0].phase     = 0.f;
                        grains_[0].phase_inc = 0.f;
                        grains_[1].active    = false;
                    }
                }
            }
        }
    }
    else
    {
        // --- C-OLA grain cluster ---
        // Spawn a new grain every `hop` OUTPUT samples (NOT scaled by speed),
        // so the cluster geometry stays constant on the output side. Each
        // grain spawns at `voice.position_` — which already advances at
        // `speed` per output sample — so the source traversal rate (= loop
        // duration) tracks `speed` alone, while per-grain pitch_ratio shifts
        // pitch without affecting how fast the loop plays.
        const float base_hop = n_eff_samples_ / static_cast<float>(std::max(1, overlap_eff_));

        synth_hop_counter_ -= 1.f;
        while (synth_hop_counter_ <= 0.f)
        {
            const size_t slot = this->find_grain_slot();
            this->spawn_cola_grain(slot, position_);
            // Per-grain hop-time jitter for random_width (±50 % of hop at depth=1).
            const float hop_jit = per_grain_jitter(grain_rng_, random_grains_, base_hop * 0.5f);
            synth_hop_counter_ += std::max(1.f, base_hop + hop_jit);
        }

        for (auto& g : grains_)
        {
            if (!g.active) continue;

            const float amp = read_window(g.win_lut_a, g.win_lut_b, g.win_blend, g.phase);

            // Per-grain level + pan with random jitter baked in.
            const float g_level = idsp::clamp(base_level_ + g.level_jit, 0.f, 1.f)
                                * env_phase_level_scale * midi_velocity_factor_;
            const float g_pan   = idsp::clamp(base_pan_ + pan_env_phase_mod + g.pan_jit, 0.f, 1.f);
            float g_pan_l, g_pan_r;
            compute_pan_gains_lut(g_pan, g_pan_l, g_pan_r);

            const float rp  = g.read_pos + read_offset;

            acc_l += amp * g_level * g_pan_l * left.read_at(rp);
            acc_r += amp * g_level * g_pan_r * right.read_at(rp);

            filter_l.process(acc_l);
            filter_r.process(acc_r);

            acc_l = filter_l.get_lowpass();
            acc_r = filter_r.get_lowpass();

            g.read_pos += g.pitch_ratio;
            // Wrap against spawn-time boundaries so a live start/length scrub
            // doesn't jump mid-window grains and produce a click. New grains
            // always spawn at the updated position_, so start changes are
            // smoothly absorbed by the grain cluster's natural overlap.
            wrap_grain_read(g, g.spawn_start, g.spawn_end);

            g.phase += g.phase_inc;
            if (g.phase >= 1.f) g.active = false;
        }
    }

    // Master clock advances after grain reads so this sample's reads use the
    // pre-advance position_ (matches existing pre-advance read semantics).
    position_ += speed;

    if (!check_bounds(effective_end)) return {0.f, 0.f};

    // --- Envelope completion edge: kill the voice once the envelope idles. ---
    if (was_active && !now_active)
    {
        active_ = false;
        return {0.f, 0.f};
    }

    return { acc_l, acc_r };
}
