#ifndef INSTRUMENT_ENVELOPE_H
#define INSTRUMENT_ENVELOPE_H

#include <cstddef>

namespace idsp
{

/** Classic ADSR / AR / AHSR envelope.
 *
 * Trigger entry points:
 *
 *   - trigger_ar(A, R)                   : Attack → Release. No sustain.
 *   - trigger_adsr(A, D, S, R)           : Attack → Decay → Sustain (held) →
 *                                          Release. Gated; call release() to
 *                                          start the release ramp.
 *   - trigger_ahr(A, H, S, R)            : Attack → Hold → Release. One-shot;
 *                                          Release fires automatically when Hold
 *                                          expires. Attack ramps to S; Hold stays
 *                                          at S; Release ramps to 0.
 *   - trigger_ahsr_gated(A, H, S, R)     : Attack → Hold → Sustain (held) →
 *                                          Release. Gated; Hold is timed (H
 *                                          samples), Sustain holds at S until
 *                                          release() is called. Attack ramps to S.
 *
 * release() begins the release stage from whatever value is current, so an
 * early note-off during Attack/Decay/Hold produces a clean ramp-down from the
 * partial value (no jump to peak first).
 *
 * Curves: linear A, linear D, linear H, exponential (RC-style) R.
 *
 * Lives in the project under namespace idsp so the eventual promotion into
 * isl/include/idsp/envelope.hpp is a pure file move.
 */
class Envelope
{
public:
    enum class Stage { Idle, Attack, Hold, Decay, Sustain, Release };

    Envelope() = default;

    /** Attack-Release envelope. Attack always completes, then Release fires. */
    inline void trigger_ar(size_t attack_samples, size_t release_samples)
    {
        attack_len_         = attack_samples;
        hold_len_           = 0;
        decay_len_          = 0;
        release_len_        = release_samples;
        sustain_level_      = 0.f;
        attack_target_      = 1.f;
        stage_sample_count_ = 0;
        value_              = 0.f;
        gated_              = false;

        if (attack_samples == 0)
        {
            value_ = 1.f;
            release_from_value_ = 1.f;
            stage_ = (release_samples == 0) ? Stage::Idle : Stage::Release;
        }
        else
        {
            stage_ = Stage::Attack;
        }
    }

    /** Gated ADSR. Holds at sustain_level after decay; call release() to
     *  start the release ramp. */
    inline void trigger_adsr(size_t attack_samples, size_t decay_samples,
                             float sustain_level, size_t release_samples)
    {
        attack_len_         = attack_samples;
        hold_len_           = 0;
        decay_len_          = decay_samples;
        release_len_        = release_samples;
        sustain_level_      = clamp01(sustain_level);
        attack_target_      = 1.f;
        stage_sample_count_ = 0;
        value_              = 0.f;
        gated_              = true;

        if (attack_samples == 0)
        {
            value_ = 1.f;
            stage_ = (decay_samples == 0) ? Stage::Sustain : Stage::Decay;
            if (stage_ == Stage::Sustain) value_ = sustain_level_;
        }
        else
        {
            stage_ = Stage::Attack;
        }
    }

    /** One-shot AHSR. Attack ramps 0 → sustain_level; Hold stays there;
     *  Release fires automatically when Hold expires. */
    inline void trigger_ahr(size_t attack_samples, size_t hold_samples,
                            float sustain_level, size_t release_samples)
    {
        attack_len_         = attack_samples;
        hold_len_           = hold_samples;
        decay_len_          = 0;
        release_len_        = release_samples;
        sustain_level_      = clamp01(sustain_level);
        attack_target_      = sustain_level_;
        stage_sample_count_ = 0;
        value_              = 0.f;
        gated_              = false;

        if (attack_samples == 0)
        {
            value_ = sustain_level_;
            if (hold_samples > 0)
            {
                stage_ = Stage::Hold;
            }
            else
            {
                release_from_value_ = sustain_level_;
                stage_ = (release_samples == 0) ? Stage::Idle : Stage::Release;
                if (stage_ == Stage::Idle) value_ = 0.f;
            }
        }
        else
        {
            stage_ = Stage::Attack;
        }
    }

    /** Gated AHSR. Attack ramps 0 → sustain_level; Hold stays there for
     *  hold_samples; then Sustain holds at sustain_level until release() is
     *  called, which starts the Release ramp. */
    inline void trigger_ahsr_gated(size_t attack_samples, size_t hold_samples,
                                   float sustain_level, size_t release_samples)
    {
        attack_len_         = attack_samples;
        hold_len_           = hold_samples;
        decay_len_          = 0;
        release_len_        = release_samples;
        sustain_level_      = clamp01(sustain_level);
        attack_target_      = sustain_level_;
        stage_sample_count_ = 0;
        value_              = 0.f;
        gated_              = true;

        if (attack_samples == 0)
        {
            value_ = sustain_level_;
            stage_ = (hold_samples > 0) ? Stage::Hold : Stage::Sustain;
        }
        else
        {
            stage_ = Stage::Attack;
        }
    }

    /** Begin release from the current value. From Idle/Release: no-op. */
    inline void release()
    {
        gated_ = false;
        if (stage_ == Stage::Idle || stage_ == Stage::Release) return;
        if (release_len_ == 0)
        {
            value_ = 0.f;
            stage_ = Stage::Idle;
            return;
        }
        release_from_value_ = value_;
        stage_sample_count_ = 0;
        stage_ = Stage::Release;
    }

    /** Advance one sample. Returns the new level in [0, 1]. */
    inline float process()
    {
        switch (stage_)
        {
            case Stage::Idle:
                return 0.f;

            case Stage::Attack:
            {
                ++stage_sample_count_;
                if (stage_sample_count_ >= attack_len_)
                {
                    value_ = attack_target_;
                    stage_sample_count_ = 0;
                    if (hold_len_ > 0)
                    {
                        stage_ = Stage::Hold;
                    }
                    else if (gated_)
                    {
                        stage_ = (decay_len_ == 0) ? Stage::Sustain : Stage::Decay;
                        if (stage_ == Stage::Sustain) value_ = sustain_level_;
                    }
                    else
                    {
                        // AR: skip decay/sustain, head straight to release.
                        release_from_value_ = attack_target_;
                        stage_ = (release_len_ == 0) ? Stage::Idle : Stage::Release;
                        if (stage_ == Stage::Idle) value_ = 0.f;
                    }
                }
                else
                {
                    value_ = static_cast<float>(stage_sample_count_)
                           / static_cast<float>(attack_len_)
                           * attack_target_;
                }
                return value_;
            }

            case Stage::Hold:
            {
                ++stage_sample_count_;
                if (stage_sample_count_ >= hold_len_)
                {
                    value_ = sustain_level_;
                    stage_sample_count_ = 0;
                    if (gated_)
                    {
                        stage_ = Stage::Sustain;
                    }
                    else
                    {
                        release_from_value_ = sustain_level_;
                        stage_ = (release_len_ == 0) ? Stage::Idle : Stage::Release;
                        if (stage_ == Stage::Idle) value_ = 0.f;
                    }
                }
                return value_;
            }

            case Stage::Decay:
            {
                ++stage_sample_count_;
                if (stage_sample_count_ >= decay_len_)
                {
                    value_ = sustain_level_;
                    stage_sample_count_ = 0;
                    stage_ = Stage::Sustain;
                }
                else
                {
                    const float t = static_cast<float>(stage_sample_count_)
                                  / static_cast<float>(decay_len_);
                    value_ = 1.f + (sustain_level_ - 1.f) * t;  // linear 1 → sustain
                }
                return value_;
            }

            case Stage::Sustain:
            {
                value_ = sustain_level_;
                return value_;
            }

            case Stage::Release:
            {
                ++stage_sample_count_;
                if (stage_sample_count_ >= release_len_)
                {
                    value_ = 0.f;
                    stage_ = Stage::Idle;
                }
                else
                {
                    // Exponential (RC-style) release: starts fast and tails off.
                    // Ramps from release_from_value_ down to 0.
                    const float t = static_cast<float>(stage_sample_count_)
                                  / static_cast<float>(release_len_);
                    const float one_minus_t = 1.f - t;
                    value_ = release_from_value_ * one_minus_t * one_minus_t;
                }
                return value_;
            }
        }
        return 0.f;
    }

    /** Last computed level, without advancing. */
    inline float current_value() const { return value_; }

    inline bool is_active() const { return stage_ != Stage::Idle; }

    /** Lifetime progress in [0, 1] across A+H+D+R. During Sustain, sits at
     *  the (H+D)→R boundary (the gate is held). Returns 0 when Idle. */
    inline float get_phase() const
    {
        const size_t total = attack_len_ + hold_len_ + decay_len_ + release_len_;
        if (total == 0) return 0.f;
        size_t elapsed = 0;
        switch (stage_)
        {
            case Stage::Attack:  elapsed = stage_sample_count_; break;
            case Stage::Hold:    elapsed = attack_len_ + stage_sample_count_; break;
            case Stage::Decay:   elapsed = attack_len_ + hold_len_ + stage_sample_count_; break;
            case Stage::Sustain: elapsed = attack_len_ + hold_len_ + decay_len_; break;
            case Stage::Release: elapsed = attack_len_ + hold_len_ + decay_len_ + stage_sample_count_; break;
            case Stage::Idle:
            default:             elapsed = 0; break;
        }
        return static_cast<float>(elapsed) / static_cast<float>(total);
    }

    inline void reset()
    {
        stage_              = Stage::Idle;
        value_              = 0.f;
        stage_sample_count_ = 0;
        gated_              = false;
    }

private:
    static inline float clamp01(float x)
    {
        return x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
    }

    Stage  stage_{Stage::Idle};
    size_t attack_len_{0};
    size_t hold_len_{0};
    size_t decay_len_{0};
    size_t release_len_{0};
    float  sustain_level_{1.f};
    float  attack_target_{1.f};
    size_t stage_sample_count_{0};
    float  release_from_value_{1.f};
    float  value_{0.f};
    bool   gated_{false};
};

} // namespace idsp

#endif
