#ifndef INSTRUMENT_ENVELOPE_H
#define INSTRUMENT_ENVELOPE_H

#include <cstddef>

namespace idsp
{

/** Shaped AHR (attack/hold/release) envelope.
 *
 * Default mode is two-stage AR: rises from 0 to 1 over attack_samples, then
 * falls from 1 to 0 over release_samples. Either stage may be zero —
 * attack=0 starts at peak (decay-only); release=0 stops at peak.
 *
 * When triggered with gated=true (typically a MIDI note-on), the envelope
 * enters a Hold stage at the peak instead of falling straight into Release.
 * Hold sustains value=1.0 indefinitely until release() is called, at which
 * point Release fires. If release() is called during Attack, the attack
 * still completes naturally and Release follows immediately — no jump.
 *
 * `shape` ∈ [0, 1] morphs the curve:
 *   0.0  — exponential (RC-style: fast rise / fast initial fall, slow tail)
 *   0.5  — linear (sharp corner at peak; clicky on short times)
 *   1.0  — logarithmic (slow start, fast end — soft swell / late fall-off)
 *
 * Lives in the project for now under namespace idsp so the eventual promotion
 * into isl/include/idsp/envelope.hpp is a pure file move.
 */
class Envelope
{
    public:
        Envelope() = default;

        /** (Re)start the envelope from 0. Lengths and shape snapshot at this
         * point — subsequent calls to process() are not affected by external
         * changes. When `gated` is true, the envelope holds at 1.0 after
         * attack completes; call release() to begin the release stage.
         */
        inline void trigger(size_t attack_samples, size_t release_samples, float shape_value = 0.0f, bool gated = false)
        {
            this->attack_len      = attack_samples;
            this->release_len     = release_samples;
            this->shape           = shape_value < 0.0f ? 0.0f : (shape_value > 1.0f ? 1.0f : shape_value);
            this->sample_count    = 0;
            this->gated           = gated;
            this->release_pending = false;

            if (attack_samples == 0)
            {
                this->value = 1.0f;
                if (gated)
                {
                    // Zero-attack gated: jump straight to Hold; release() will move us on.
                    this->stage = Stage::Hold;
                }
                else
                {
                    this->stage = (release_samples == 0) ? Stage::Idle : Stage::Release;
                }
            }
            else
            {
                this->value = 0.0f;
                this->stage = Stage::Attack;
            }
        }

        /** Close the gate. Behaviour depends on the current stage:
         *   - Attack  → mark release_pending; attack runs to completion, then release fires.
         *   - Hold    → switch to Release immediately, counting from 1.0.
         *   - Idle/Release → no-op.
         */
        inline void release()
        {
            this->gated = false;
            switch (this->stage)
            {
                case Stage::Attack:
                    this->release_pending = true;
                    break;
                case Stage::Hold:
                    this->stage = (this->release_len == 0) ? Stage::Idle : Stage::Release;
                    this->sample_count = 0;
                    if (this->stage == Stage::Idle) this->value = 0.0f;
                    break;
                case Stage::Idle:
                case Stage::Release:
                default:
                    break;
            }
        }

        /** Advance one sample. Returns the new level in [0, 1]. */
        inline Sample process()
        {
            switch (this->stage)
            {
                case Stage::Idle:
                    return 0.0f;

                case Stage::Attack:
                {
                    ++this->sample_count;
                    if (this->sample_count >= this->attack_len)
                    {
                        this->value = 1.0f;
                        this->sample_count = 0;
                        // Gate still held and no pending release ⇒ enter Hold.
                        // Otherwise fall through to Release (or Idle if release_len==0).
                        if (this->gated && !this->release_pending)
                        {
                            this->stage = Stage::Hold;
                        }
                        else
                        {
                            this->stage = (this->release_len == 0) ? Stage::Idle : Stage::Release;
                        }
                    }
                    else
                    {
                        const Sample t = static_cast<Sample>(this->sample_count)
                                       / static_cast<Sample>(this->attack_len);
                        this->value = shape_attack(t, this->shape);
                    }
                    return this->value;
                }

                case Stage::Hold:
                {
                    // Sustain at peak until release() is called externally.
                    this->value = 1.0f;
                    return this->value;
                }

                case Stage::Release:
                {
                    ++this->sample_count;
                    if (this->sample_count >= this->release_len)
                    {
                        this->value = 0.0f;
                        this->stage = Stage::Idle;
                    }
                    else
                    {
                        const Sample t = static_cast<Sample>(this->sample_count)
                                       / static_cast<Sample>(this->release_len);
                        this->value = shape_release(t, this->shape);
                    }
                    return this->value;
                }
            }
            return 0.0f;
        }

        /** Last computed level, without advancing. */
        inline Sample current_value() const { return this->value; }

        inline bool is_active() const { return this->stage != Stage::Idle; }

        /** Lifetime progress in [0, 1] across the full attack+release window.
         *  Returns 0 when Idle. During Hold, returns the attack/total ratio —
         *  i.e. sits at the A↔R boundary until release() advances us. */
        inline float get_phase() const
        {
            const size_t total = this->attack_len + this->release_len;
            if (total == 0) return 0.f;
            size_t elapsed = 0;
            switch (this->stage)
            {
                case Stage::Attack:  elapsed = this->sample_count; break;
                case Stage::Hold:    elapsed = this->attack_len; break;
                case Stage::Release: elapsed = this->attack_len + this->sample_count; break;
                case Stage::Idle:
                default:             elapsed = 0; break;
            }
            return static_cast<float>(elapsed) / static_cast<float>(total);
        }

        inline void reset()
        {
            this->stage           = Stage::Idle;
            this->value           = 0.0f;
            this->sample_count    = 0;
            this->gated           = false;
            this->release_pending = false;
        }

    private:
        enum class Stage { Idle, Attack, Hold, Release };

        // Crossfades three anchor curves of t ∈ [0,1]:
        //   shape=0  : exponential (RC-style — concave-down attack, concave-up release)
        //   shape=0.5: linear
        //   shape=1  : logarithmic (concave-up attack, concave-down release)
        // The exp/log anchors are quadratic approximations: t^2 and 1-(1-t)^2.
        // These are mirror images about y=x, so the morph is symmetric around
        // the linear midpoint.
        static inline Sample shape_attack(Sample t, float shape)
        {
            const Sample one_minus_t = Sample(1) - t;
            const Sample expc = Sample(1) - one_minus_t * one_minus_t;  // 1-(1-t)^2  (concave down)
            const Sample lin  = t;
            const Sample logc = t * t;                                  // t^2        (concave up)

            if (shape < 0.5f)
            {
                const Sample w = Sample(2) * shape;
                return (Sample(1) - w) * expc + w * lin;
            }
            const Sample w = Sample(2) * (shape - Sample(0.5));
            return (Sample(1) - w) * lin + w * logc;
        }

        // Release: t ∈ [0,1], value goes 1→0. Mirror of attack about y=0.5 so
        // shape=0 gives the natural exponential decay (steep start, slow tail).
        static inline Sample shape_release(Sample t, float shape)
        {
            const Sample one_minus_t = Sample(1) - t;
            const Sample expc = one_minus_t * one_minus_t;              // (1-t)^2    (concave up)
            const Sample lin  = one_minus_t;
            const Sample logc = Sample(1) - t * t;                      // 1 - t^2    (concave down)

            if (shape < 0.5f)
            {
                const Sample w = Sample(2) * shape;
                return (Sample(1) - w) * expc + w * lin;
            }
            const Sample w = Sample(2) * (shape - Sample(0.5));
            return (Sample(1) - w) * lin + w * logc;
        }

        Stage  stage{Stage::Idle};
        size_t attack_len{0};
        size_t release_len{0};
        size_t sample_count{0};
        Sample value{0.0f};
        float  shape{0.0f};
        bool   gated{false};
        bool   release_pending{false};
};

} // namespace idsp

#endif
