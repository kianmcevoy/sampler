#ifndef INSTRUMENT_VOICE_HPP
#define INSTRUMENT_VOICE_HPP

#include "instrument/envelope.hpp"
#include "idsp/delay.hpp"

#include <cstddef>
#include <cstdint>

/** Polyphonic sampler voice.
 *
 * Each Voice owns its own playback position, envelope, and routing depths.
 * The audio buffer (stereo LagrangeDelay pair) is shared across voices and
 * passed by const reference into process().
 *
 * Parameters fall into two groups:
 *   - **Base** values (start/length/speed/level/pan) are snapshotted at
 *     trigger() and frozen for the voice's lifetime. Slider changes during
 *     playback do not disturb in-flight voices.
 *   - **Envelope routing depths** are also snapshotted and applied per-sample
 *     inside process(); each destination has a depth in [-1, +1].
 *
 * Direction of playback is locked at trigger: env-speed modulation changes
 * magnitude only, never sign.
 */
class Voice
{
public:
    struct TriggerParams
    {
        size_t start_pos;
        size_t end_pos;
        size_t length;
        float  base_speed;
        float  base_level;
        float  base_pan;
        bool   sample_loops;

        // Envelope (durations pre-resolved by caller per sync mode)
        size_t env_attack;
        size_t env_release;
        float  env_shape;
        bool   env_loops;
        bool   env_sync;

        // Per-destination depths in [-1, +1]
        float  depth_speed;
        float  depth_start;
        float  depth_length;
        float  depth_level;
        float  depth_pan;
    };

    struct StereoFrame { float l; float r; };

    void  trigger(const TriggerParams& p, uint64_t seq);
    void  kill();

    bool     is_active()  const { return active_; }
    uint64_t launch_seq() const { return launch_seq_; }
    float    position()   const { return position_; }
    float    env_value()  const { return envelope_.current_value(); }

    StereoFrame process(const idsp::LagrangeDelay<524288>& left,
                        const idsp::LagrangeDelay<524288>& right);

private:
    bool check_bounds(float effective_end);
    void retrigger_position();

    // Frozen at trigger
    size_t start_pos_{0};
    size_t end_pos_{0};
    size_t length_{0};
    float  base_speed_{0.f};
    float  base_level_{0.f};
    float  base_pan_{0.5f};
    float  base_pan_l_{0.f};
    float  base_pan_r_{0.f};
    bool   sample_loops_{false};
    bool   forward_{true};

    size_t env_attack_{0};
    size_t env_release_{0};
    float  env_shape_{0.f};
    bool   env_loops_{false};
    bool   env_sync_{false};

    float depth_speed_{0.f};
    float depth_start_{0.f};
    float depth_length_{0.f};
    float depth_level_{0.f};
    float depth_pan_{0.f};

    // Live state
    float    position_{0.f};
    bool     active_{false};
    uint64_t launch_seq_{0};
    idsp::Envelope envelope_{};
};

#endif
