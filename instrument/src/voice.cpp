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

    constexpr float kTwoPi = 6.28318530717958647692f;
    constexpr float kPi    = 3.14159265358979323846f;

    // 4-way morphed grain window. `shape`∈[0,1] interpolates through:
    //   0.00 → rectangle (constant 1)
    //   0.33 → down-ramp  (1 → 0)
    //   0.66 → cosine     (Hann: 0 → 1 → 0)
    //   1.00 → up-ramp    (0 → 1)
    // `phase`∈[0,1) is the grain's progress through its window.
    inline float window_value(float shape, float phase)
    {
        const float rect = 1.f;
        const float down = 1.f - phase;
        const float cosw = 0.5f - 0.5f * std::cos(kTwoPi * phase);
        const float up   = phase;

        const float s = idsp::clamp(shape, 0.f, 1.f) * 3.f;
        int seg = static_cast<int>(s);
        if (seg < 0) seg = 0;
        if (seg > 2) seg = 2;
        const float frac = s - static_cast<float>(seg);
        const float a = (seg == 0) ? rect : (seg == 1) ? down : cosw;
        const float b = (seg == 0) ? down : (seg == 1) ? cosw : up;
        return (1.f - frac) * a + frac * b;
    }

    // Crossfade ramp curve used in width=1 mode. `t`∈[0,1] is fade phase
    // (0 = silent, 1 = full). At `shape`≈0 it's a hard step at the midpoint
    // (i.e. no real crossfade — the boundary just hard-cuts). Higher shape
    // values morph from linear ramp into an equal-power-ish cosine ramp.
    inline float ramp_curve(float shape, float t)
    {
        if (shape < 0.001f) return (t >= 0.5f) ? 1.f : 0.f;
        const float linear = t;
        const float cosw   = 0.5f - 0.5f * std::cos(kPi * t);  // 0→1, raised cosine
        const float blend  = idsp::clamp(shape, 0.f, 1.f);
        return (1.f - blend) * linear + blend * cosw;
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

    // Speed and direction both follow the slider live. `forward_` is only
    // "locked at trigger" with respect to envelope/phase modulation — those
    // can't flip direction, but a deliberate slider edit can.
    forward_    = (p.speed >= 0.f);
    base_speed_ = p.speed;

    base_level_ = p.level;
    base_pan_   = p.pan;
    compute_pan_gains_lut(p.pan, base_pan_l_, base_pan_r_);
    sample_loops_ = p.loop;

    // Envelope durations:
    //   loop ON  → absolute time (0..5 s) — sample loops underneath.
    //   loop OFF → fraction of the playback range — so the envelope fits
    //              the actual length of audio we'll play through.
    const auto resolve_dur = [&](float slider_value) -> size_t
    {
        const float scale = sample_loops_
            ? 5.f * sample_rate
            : static_cast<float>(loop_len);
        const auto raw = static_cast<size_t>(idsp::clamp(slider_value, 0.f, 1.f) * scale);
        return raw == 0 ? 1 : raw;
    };

    env_attack_        = resolve_dur(p.attack);
    env_decay_         = resolve_dur(p.decay);
    env_release_       = resolve_dur(p.release);
    env_sustain_level_ = idsp::clamp(p.sustain, 0.f, 1.f);

    depth_speed_  = p.envelope_speed;
    depth_start_  = p.envelope_start;
    depth_length_ = p.envelope_length;
    depth_level_  = p.envelope_level;
    depth_pan_    = p.envelope_pan;

    depth_phase_speed_  = p.phase_speed;
    depth_phase_start_  = p.phase_start;
    depth_phase_length_ = p.phase_length;
    depth_phase_level_  = p.phase_level;
    depth_phase_pan_    = p.phase_pan;

    // Granular params. Pitch direction is independent of speed direction —
    // you can read grains backwards through a forward-playing voice. Window
    // size/shape/width are picked up live; running grains keep their existing
    // phase_inc until they finish (only newly-spawned grains see the new size).
    base_pitch_   = p.pitch;
    window_size_  = idsp::clamp(p.window_size, 0.1f, 1.f);
    window_shape_ = idsp::clamp(p.window_shape, 0.f, 1.f);
    width_        = idsp::clamp(p.width, 1.f, 8.f);
    sample_rate_  = sample_rate;
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

void Voice::spawn_grain(size_t slot, Grain::Role role, float read_pos, float phase_inc)
{
    if (slot >= grains_.size()) return;
    auto& g = grains_[slot];
    g.active       = true;
    g.role         = role;
    g.read_pos     = read_pos;
    g.phase        = 0.f;
    g.phase_inc    = phase_inc;
    g.spawned_next = false;
}

void Voice::wrap_grain_read(Grain& g, float start_f, float end_f) const
{
    const float duration_f = end_f - start_f;
    if (duration_f <= 0.f) return;
    if (g.read_pos >= end_f)
    {
        if (sample_loops_)
        {
            while (g.read_pos >= end_f) g.read_pos -= duration_f;
            if (g.read_pos < start_f) g.read_pos = start_f;
        }
        else { g.active = false; }
    }
    else if (g.read_pos < start_f)
    {
        if (sample_loops_)
        {
            while (g.read_pos < start_f) g.read_pos += duration_f;
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

    // Initialise the grain cluster: kill any leftover state and spawn slot 0.
    // For width=1 it stays Body forever (until the loop-boundary crossfade
    // promotes it to FadeOut). For width>=2 it spawns the chain by hitting
    // phase=1/width and waking slot 1, which wakes slot 2, etc.
    this->reset_grains();
    const int width = std::max(1, std::min(8, static_cast<int>(std::lround(width_))));
    last_width_ = width;
    const float window_samples = std::max(1.f, window_size_ * sample_rate_);
    if (width == 1)
    {
        // Body grain: phase doesn't advance; amp stays at 1 until crossfade.
        this->spawn_grain(0, Grain::Role::Body, position_, 0.f);
    }
    else
    {
        this->spawn_grain(0, Grain::Role::Body, position_, 1.f / window_samples);
    }

    active_     = (length_ > 0);
    launch_seq_ = seq;
}

void Voice::trigger_plain(const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq)
{
    this->prepare_for_trigger(p, buffer_size, sample_rate, seq);
    envelope_mode_ = EnvelopeMode::None;
    envelope_.reset();
}

void Voice::trigger_ar(const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq)
{
    this->prepare_for_trigger(p, buffer_size, sample_rate, seq);
    envelope_mode_ = EnvelopeMode::AR;
    envelope_.trigger_ar(env_attack_, env_release_);
}

void Voice::trigger_adsr_gated(const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq)
{
    this->prepare_for_trigger(p, buffer_size, sample_rate, seq);
    envelope_mode_ = EnvelopeMode::ADSR;
    envelope_.trigger_adsr(env_attack_, env_decay_, env_sustain_level_, env_release_);
}

void Voice::kill()
{
    active_ = false;
    envelope_.reset();
}

void Voice::release()
{
    if (envelope_mode_ == EnvelopeMode::ADSR) envelope_.release();
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

    if (position_ >= effective_end)
    {
        if (sample_loops_)
        {
            while (position_ >= effective_end) position_ -= duration_f;
            if (position_ < start_f) position_ = start_f;
        }
        else { active_ = false; return false; }
    }
    else if (position_ < start_f)
    {
        if (sample_loops_)
        {
            while (position_ < start_f) position_ += duration_f;
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

    // --- Auto-release predictor (ADSR + !loop): start the release ramp
    // `release_len_` samples before we'd hit the end, so it completes in time.
    if (envelope_mode_ == EnvelopeMode::ADSR && !sample_loops_ && envelope_.is_active())
    {
        const float speed_mag = (base_speed_ >= 0.f) ? base_speed_ : -base_speed_;
        if (speed_mag > 0.f)
        {
            const float samples_left = forward_
                ? (static_cast<float>(end_pos_) - position_)
                : (position_ - static_cast<float>(start_pos_));
            const float time_left_samples = samples_left / speed_mag;
            if (time_left_samples <= static_cast<float>(env_release_))
            {
                envelope_.release();
            }
        }
    }

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

    // --- Level (multiplicative VCA; envelope and phase mods multiply) ---
    // depth_level_ acts as wet/dry between "ignore envelope" (d=0 → factor=1)
    // and "envelope is the VCA" (d=±1 → factor=e or 1-e). For None-mode
    // voices we skip envelope_level entirely, since e is pinned to 0 and a
    // non-zero depth would silence the voice.
    float env_level_mod;
    if (depth_level_ >= 0.f) env_level_mod = (1.f - depth_level_) + depth_level_ * e;
    else                     env_level_mod = 1.f + depth_level_ * (1.f - e);

    float phase_level_mod;
    if (depth_phase_level_ >= 0.f) phase_level_mod = (1.f - depth_phase_level_) + depth_phase_level_ * ph;
    else                           phase_level_mod = 1.f + depth_phase_level_ * (1.f - ph);

    const float voice_level = (envelope_mode_ == EnvelopeMode::None)
        ? base_level_ * phase_level_mod
        : base_level_ * env_level_mod * phase_level_mod;

    // --- Pan (LUT, skipped when unmodulated) ---
    float pan_l, pan_r;
    if (depth_pan_ == 0.f && depth_phase_pan_ == 0.f)
    {
        pan_l = base_pan_l_;
        pan_r = base_pan_r_;
    }
    else
    {
        const float pan = idsp::clamp(base_pan_ + depth_pan_ * e + depth_phase_pan_ * ph, 0.f, 1.f);
        compute_pan_gains_lut(pan, pan_l, pan_r);
    }

    // --- Granular cluster read & advance ---
    const float start_f = static_cast<float>(start_pos_);
    const float end_f   = effective_end;

    // Pitch sign locked at trigger (matches `forward_`). Magnitude editable.
    // base_pitch_ already carries the right sign because set_live_params()
    // applies `forward_ ? +mag : -mag`.
    const float pitch_eff = base_pitch_;

    const int width = std::max(1, std::min(8, static_cast<int>(std::lround(width_))));
    if (width != last_width_)
    {
        // Mode flip (1↔N or N→M). Reset the cluster and respawn slot 0 so
        // the new topology starts cleanly. Brief glitch acceptable.
        this->reset_grains();
        const float ws = std::max(1.f, window_size_ * sample_rate_);
        const float phase_inc0 = (width == 1) ? 0.f : (1.f / ws);
        this->spawn_grain(0, Grain::Role::Body, position_, phase_inc0);
        last_width_ = width;
    }

    float acc_l = 0.f, acc_r = 0.f;
    const float window_samples      = std::max(1.f, window_size_ * sample_rate_);
    const float half_window_samples = 0.5f * window_samples;

    if (width == 1)
    {
        // --- width=1: continuous playhead with loop-boundary crossfade ---
        // Trigger crossfade if loop is on, Body exists, and the master clock
        // is within half_window of the boundary. Skip the crossfade entirely
        // when window_shape is at the rectangle end (hard-cut behaviour).
        if (sample_loops_ && grains_[0].active && grains_[0].role == Grain::Role::Body
            && !grains_[1].active && window_shape_ > 0.001f)
        {
            const float dist = forward_ ? (end_f - position_) : (position_ - start_f);
            if (dist > 0.f && dist <= half_window_samples)
            {
                grains_[0].role      = Grain::Role::FadeOut;
                grains_[0].phase     = 0.f;
                grains_[0].phase_inc = 1.f / half_window_samples;
                this->spawn_grain(1, Grain::Role::FadeIn,
                                  forward_ ? start_f : (end_f - 1.f),
                                  1.f / half_window_samples);
            }
        }

        for (size_t gi = 0; gi < 2; ++gi)
        {
            auto& g = grains_[gi];
            if (!g.active) continue;

            float amp;
            if      (g.role == Grain::Role::FadeOut) amp = ramp_curve(window_shape_, 1.f - g.phase);
            else if (g.role == Grain::Role::FadeIn)  amp = ramp_curve(window_shape_, g.phase);
            else                                     amp = 1.f;

            const float rp = g.read_pos + read_offset;
            acc_l += amp * left.read_at(rp);
            acc_r += amp * right.read_at(rp);

            g.read_pos += pitch_eff;
            wrap_grain_read(g, start_f, end_f);

            if (g.role != Grain::Role::Body)
            {
                g.phase += g.phase_inc;
                if (g.phase >= 1.f && g.role == Grain::Role::FadeOut)
                {
                    g.active = false;
                    if (grains_[1].active)
                    {
                        grains_[0]            = grains_[1];
                        grains_[0].role       = Grain::Role::Body;
                        grains_[0].phase      = 0.f;
                        grains_[0].phase_inc  = 0.f;
                        grains_[0].spawned_next = false;
                        grains_[1].active = false;
                    }
                }
            }
        }

        // Body died (e.g. wrapped out of a non-looping voice). The voice's
        // own check_bounds below will kill it.
    }
    else
    {
        // --- width >= 2: continuous granular cluster ---
        const float trigger_phase = 1.f / static_cast<float>(width);
        for (size_t gi = 0; gi < grains_.size(); ++gi)
        {
            auto& g = grains_[gi];
            if (!g.active) continue;

            const float amp = window_value(window_shape_, g.phase);
            const float rp  = g.read_pos + read_offset;
            acc_l += amp * left.read_at(rp);
            acc_r += amp * right.read_at(rp);

            g.read_pos += pitch_eff;
            wrap_grain_read(g, start_f, end_f);

            g.phase += g.phase_inc;

            if (!g.spawned_next && g.phase >= trigger_phase)
            {
                const size_t next = (gi + 1) % static_cast<size_t>(width);
                if (!grains_[next].active)
                {
                    this->spawn_grain(next, Grain::Role::Body, position_, 1.f / window_samples);
                }
                g.spawned_next = true;
            }

            if (g.phase >= 1.f) g.active = false;
        }

        // Safety net: if the whole cluster died (e.g. width was just raised
        // or sample_rate/window changed), respawn slot 0 so the voice keeps
        // making sound until check_bounds kills it.
        bool any_alive = false;
        for (int i = 0; i < width; ++i) { if (grains_[i].active) { any_alive = true; break; } }
        if (!any_alive)
        {
            this->spawn_grain(0, Grain::Role::Body, position_, 1.f / window_samples);
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

    return { voice_level * pan_l * acc_l, voice_level * pan_r * acc_r };
}
