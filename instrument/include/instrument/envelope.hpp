#ifndef INSTRUMENT_ENVELOPE_H
#define INSTRUMENT_ENVELOPE_H

#include <cstddef>

namespace idsp
{

/** Shaped AR (attack/release) envelope.
 *
 * Two-stage envelope: rises from 0 to 1 over attack_samples, then falls
 * from 1 to 0 over release_samples. Either stage may be zero — attack=0
 * starts at peak (decay-only); release=0 stops at peak.
 *
 * `shape` ∈ [0, 1] morphs the curve:
 *   0.0  — exponential (RC-style: fast rise / fast initial fall, slow tail)
 *   0.5  — linear (sharp corner at peak; clicky on short times)
 *   1.0  — logarithmic (slow start, fast end — soft swell / late fall-off)
 *
 * The class is unaware of gates or retriggering — call trigger() to (re)start.
 * Lives in the project for now under namespace idsp so the eventual promotion
 * into isl/include/idsp/envelope.hpp is a pure file move.
 */
class Envelope
{
    public:
        Envelope() = default;

        /** (Re)start the envelope from 0. Lengths and shape snapshot at this
         * point — subsequent calls to process() are not affected by external
         * changes.
         */
        inline void trigger(size_t attack_samples, size_t release_samples, float shape_value = 0.0f)
        {
            this->attack_len   = attack_samples;
            this->release_len  = release_samples;
            this->shape        = shape_value < 0.0f ? 0.0f : (shape_value > 1.0f ? 1.0f : shape_value);
            this->sample_count = 0;

            if (attack_samples == 0)
            {
                this->value = 1.0f;
                this->stage = (release_samples == 0) ? Stage::Idle : Stage::Release;
            }
            else
            {
                this->value = 0.0f;
                this->stage = Stage::Attack;
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
                        this->stage = (this->release_len == 0) ? Stage::Idle : Stage::Release;
                    }
                    else
                    {
                        const Sample t = static_cast<Sample>(this->sample_count)
                                       / static_cast<Sample>(this->attack_len);
                        this->value = shape_attack(t, this->shape);
                    }
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
         *  Returns 0 when Idle. */
        inline float get_phase() const
        {
            const size_t total = this->attack_len + this->release_len;
            if (total == 0) return 0.f;
            const size_t elapsed = (this->stage == Stage::Release)
                ? this->attack_len + this->sample_count
                : (this->stage == Stage::Attack ? this->sample_count : 0);
            return static_cast<float>(elapsed) / static_cast<float>(total);
        }

        inline void reset()
        {
            this->stage        = Stage::Idle;
            this->value        = 0.0f;
            this->sample_count = 0;
        }

    private:
        enum class Stage { Idle, Attack, Release };

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
};

} // namespace idsp

#endif
