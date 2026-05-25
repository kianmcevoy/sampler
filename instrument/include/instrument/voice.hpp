#ifndef INSTRUMENT_VOICE_HPP
#define INSTRUMENT_VOICE_HPP

#include "instrument/envelope.hpp"
#include "instrument/parameter_data.hpp"
#include "idsp/delay.hpp"

#include <cstddef>
#include <cstdint>

/** Polyphonic sampler voice.
 *
 * Each Voice owns its own playback position, envelope, and routing depths.
 * The audio buffer (stereo LagrangeDelay pair) is shared across voices and
 * passed by const reference into process().
 *
 * Parameter model:
 *   - Direction (`forward_`) is the only field truly frozen at trigger —
 *     env-speed modulation can change magnitude but never sign.
 *   - All other base parameters are kept up-to-date via `set_live_params`,
 *     which the Instrument calls each block from the per-voice live-param
 *     slot (so edits to the currently-selected voice take effect immediately).
 *   - Envelope durations (attack/release/shape) are recomputed by
 *     `set_live_params`, but the currently-running envelope phase keeps its
 *     original counters — new envelope durations apply at the next retrigger
 *     boundary (i.e. on a loop or a fresh `trigger()`).
 */
class Voice
{
public:
    struct StereoFrame { float l; float r; };

    /** Begin playback. `p` carries the post-random *effective* live params for
     * this launch; `buffer_size`/`sample_rate` are needed to resolve
     * fractional start/length/time into sample counts. Direction is locked
     * from the sign of `p.speed`. `gated=true` puts the envelope in AHR mode
     * (holds at peak after attack); use release() to begin the release. */
    void  trigger(const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq, bool gated = false);

    /** Apply a fresh live-param snapshot without retriggering. Position,
     * envelope phase, direction, and active state are preserved. */
    void  set_live_params(const VoiceLiveParams& p, size_t buffer_size, float sample_rate);

    void  kill();

    /** Release the envelope gate. Used by the MIDI path on note-off. If the
     *  voice was triggered ungated this is a no-op. */
    void  release();

    /** Retrigger the envelope alone — position, direction, base params untouched.
     *  Used by the envelope_trigger button when envelope_sync is OFF. */
    void  trigger_envelope();

    bool     is_active()  const { return active_; }
    uint64_t launch_seq() const { return launch_seq_; }
    float    position()   const { return position_; }
    float    env_value()  const { return envelope_.current_value(); }
    float    env_phase()  const { return envelope_.get_phase(); }
    float    phase()      const;
    /** Current output amplitude: envelope value × base level. 0 when inactive.
     *  Drives voice-button LED brightness so it fades with the envelope. */
    float    current_level() const { return active_ ? envelope_.current_value() * base_level_ : 0.f; }

    StereoFrame process(const idsp::LagrangeDelay<524288>& left,
                        const idsp::LagrangeDelay<524288>& right);

private:
    bool check_bounds(float effective_end);
    void retrigger_position();

    // Live-editable base params (re-derived from VoiceLiveParams each block)
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

    float depth_phase_speed_{0.f};
    float depth_phase_start_{0.f};
    float depth_phase_length_{0.f};
    float depth_phase_level_{0.f};
    float depth_phase_pan_{0.f};

    // Live state
    float    position_{0.f};
    bool     active_{false};
    uint64_t launch_seq_{0};
    idsp::Envelope envelope_{};
};

#endif
