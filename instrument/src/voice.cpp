#include "instrument/voice.hpp"

#include "idsp/functions.hpp"
#include "idsp/lookup.hpp"

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

    // Speed magnitude is editable; direction stays locked to `forward_`.
    const float mag = (p.speed >= 0.f) ? p.speed : -p.speed;
    base_speed_ = forward_ ? mag : -mag;

    base_level_ = p.level;
    base_pan_   = p.pan;
    compute_pan_gains_lut(p.pan, base_pan_l_, base_pan_r_);
    sample_loops_ = p.loop;

    // Envelope durations: sync ON ⇒ fraction of (current) loop length;
    // sync OFF ⇒ 0..5 s. Recomputed but only applied on the next retrigger
    // boundary — the running envelope keeps its original attack/release.
    const size_t env_dur_raw = p.envelope_sync
        ? static_cast<size_t>(p.time * static_cast<float>(loop_len))
        : static_cast<size_t>(p.time * 5.0f * sample_rate);
    const size_t env_dur = (env_dur_raw > 0) ? env_dur_raw : 1;

    env_attack_  = static_cast<size_t>(p.skew * static_cast<float>(env_dur));
    env_release_ = (env_dur > env_attack_) ? (env_dur - env_attack_) : 0;
    env_shape_   = p.shape;
    env_loops_   = p.loop_envelope;
    env_sync_    = p.envelope_sync;

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

void Voice::trigger(const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq, bool gated)
{
    forward_ = (p.speed >= 0.f);
    this->set_live_params(p, buffer_size, sample_rate);
    this->retrigger_position();
    active_     = (length_ > 0);
    launch_seq_ = seq;
    envelope_.trigger(env_attack_, env_release_, env_shape_, gated);
}

void Voice::kill()
{
    active_ = false;
    envelope_.reset();
}

void Voice::release()
{
    envelope_.release();
}

void Voice::trigger_envelope()
{
    envelope_.trigger(env_attack_, env_release_, env_shape_);
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

    const bool was_active = envelope_.is_active();
    const float e = envelope_.process();
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

    // --- Start (read-position offset) ---
    const float start_mod = depth_start_ * e + depth_phase_start_ * ph;
    const float read_pos  = (start_mod != 0.f)
        ? position_ + start_mod * static_cast<float>(length_)
        : position_;

    // --- Length (dynamic effective end) ---
    const float length_mod   = depth_length_ * e + depth_phase_length_ * ph;
    const float effective_end = (length_mod != 0.f)
        ? static_cast<float>(end_pos_) + length_mod * static_cast<float>(length_)
        : static_cast<float>(end_pos_);

    // --- Level (multiplicative VCA; envelope and phase mods multiply) ---
    float env_level_mod;
    if (depth_level_ >= 0.f) env_level_mod = (1.f - depth_level_) + depth_level_ * e;
    else                     env_level_mod = 1.f + depth_level_ * (1.f - e);

    float phase_level_mod;
    if (depth_phase_level_ >= 0.f) phase_level_mod = (1.f - depth_phase_level_) + depth_phase_level_ * ph;
    else                           phase_level_mod = 1.f + depth_phase_level_ * (1.f - ph);

    const float level = base_level_ * env_level_mod * phase_level_mod;

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

    // --- Read & advance ---
    const float l = left.read_at(read_pos);
    const float r = right.read_at(read_pos);
    position_ += speed;

    if (!check_bounds(effective_end)) return {0.f, 0.f};

    // --- Envelope completion edge (was active, now idle) ---
    if (was_active && !now_active)
    {
        if (env_loops_)
        {
            envelope_.trigger(env_attack_, env_release_, env_shape_);
            if (env_sync_) retrigger_position();
        }
    }

    return { level * pan_l * l, level * pan_r * r };
}
