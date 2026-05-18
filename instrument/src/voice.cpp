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

void Voice::trigger(const TriggerParams& p, uint64_t seq)
{
    start_pos_    = p.start_pos;
    end_pos_      = p.end_pos;
    length_       = p.length;
    base_speed_   = p.base_speed;
    base_level_   = p.base_level;
    base_pan_     = p.base_pan;
    compute_pan_gains_lut(p.base_pan, base_pan_l_, base_pan_r_);
    sample_loops_ = p.sample_loops;
    forward_      = (p.base_speed >= 0.f);

    env_attack_  = p.env_attack;
    env_release_ = p.env_release;
    env_shape_   = p.env_shape;
    env_loops_   = p.env_loops;
    env_sync_    = p.env_sync;

    depth_speed_  = p.depth_speed;
    depth_start_  = p.depth_start;
    depth_length_ = p.depth_length;
    depth_level_  = p.depth_level;
    depth_pan_    = p.depth_pan;

    retrigger_position();
    active_     = (length_ > 0);
    launch_seq_ = seq;
    envelope_.trigger(env_attack_, env_release_, env_shape_);
}

void Voice::kill()
{
    active_ = false;
    envelope_.reset();
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

    // --- Speed (magnitude only; direction locked at trigger) ---
    float speed = base_speed_;
    if (depth_speed_ != 0.f)
    {
        const float base_mag = forward_ ? base_speed_ : -base_speed_;
        float mag = base_mag + depth_speed_ * e * 4.f;
        if (mag < 0.f) mag = 0.f;
        speed = forward_ ? mag : -mag;
    }

    // --- Start (read-position offset) ---
    const float read_pos = (depth_start_ != 0.f)
        ? position_ + depth_start_ * e * static_cast<float>(length_)
        : position_;

    // --- Length (dynamic effective end) ---
    const float effective_end = (depth_length_ != 0.f)
        ? static_cast<float>(end_pos_) + depth_length_ * e * static_cast<float>(length_)
        : static_cast<float>(end_pos_);

    // --- Level (multiplicative VCA) ---
    float level_mod;
    if (depth_level_ >= 0.f) level_mod = (1.f - depth_level_) + depth_level_ * e;
    else                     level_mod = 1.f + depth_level_ * (1.f - e);
    const float level = base_level_ * level_mod;

    // --- Pan (LUT, skipped when unmodulated) ---
    float pan_l, pan_r;
    if (depth_pan_ == 0.f)
    {
        pan_l = base_pan_l_;
        pan_r = base_pan_r_;
    }
    else
    {
        const float pan = idsp::clamp(base_pan_ + depth_pan_ * e, 0.f, 1.f);
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
        else
        {
            active_ = false;
        }
    }

    return { level * pan_l * l, level * pan_r * r };
}
