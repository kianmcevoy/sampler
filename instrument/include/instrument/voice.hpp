#ifndef INSTRUMENT_VOICE_HPP
#define INSTRUMENT_VOICE_HPP

#include "instrument/envelope.hpp"
#include "instrument/parameter_data.hpp"
#include "idsp/delay.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

/** Polyphonic sampler voice.
 *
 * Each Voice owns its own playback position, envelope, and routing depths.
 * The audio buffer (stereo LagrangeDelay pair) is shared across voices and
 * passed by const reference into process().
 *
 * Envelope contract is fixed at launch via one of three triggers:
 *   - trigger_plain        : no envelope (play button). Voice level = base.
 *                            Terminates at end of sample if !loop; else runs
 *                            forever until kill/steal.
 *   - trigger_ar           : Attack → Release (envelope_trigger button). Voice
 *                            terminates when envelope returns to idle.
 *   - trigger_adsr_gated   : Attack → Decay → Sustain (held) → Release on
 *                            release(). MIDI note-on. If !loop, release fires
 *                            automatically `release_len_` samples before
 *                            end-of-sample so the ramp completes.
 *
 * Parameter model:
 *   - Envelope/phase speed modulation cannot flip direction (clamped at 0
 *     before reapplying `forward_`'s sign), but a deliberate slider edit can
 *     — `set_live_params` re-derives `forward_` from the current speed sign.
 *   - All other base parameters are kept up-to-date via `set_live_params`,
 *     which the Instrument calls each block from the per-voice live-param
 *     slot (so edits to the currently-selected voice take effect immediately).
 *   - Envelope durations are recomputed by `set_live_params`, but the
 *     currently-running envelope phase keeps its original counters — new
 *     durations apply at the next retrigger.
 */
class Voice
{
public:
    enum class EnvelopeMode { None, AR, ADSR };

    struct StereoFrame { float l; float r; };

    void trigger_plain      (const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq);
    void trigger_ar         (const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq);
    void trigger_adsr_gated (const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq);

    /** Apply a fresh live-param snapshot without retriggering. */
    void set_live_params(const VoiceLiveParams& p, size_t buffer_size, float sample_rate);

    void kill();

    /** Begin release. Only meaningful in ADSR mode; no-op otherwise so a
     *  stray MIDI note-off can't disturb a play/envelope-trigger voice. */
    void release();

    bool         is_active()     const { return active_; }
    uint64_t     launch_seq()    const { return launch_seq_; }
    float        position()      const { return position_; }
    EnvelopeMode envelope_mode() const { return envelope_mode_; }
    float        env_value()     const { return envelope_.current_value(); }
    float        env_phase()     const { return envelope_.get_phase(); }
    float        phase()         const;
    /** Current output amplitude (drives voice-button LED brightness). */
    float        current_level() const;

    StereoFrame process(const idsp::LagrangeDelay<524288>& left,
                        const idsp::LagrangeDelay<524288>& right);

private:
    // A single windowed grain inside the voice's grain cluster.
    // In width=1 mode, slot 0 holds the Body (or FadeOut during a crossfade)
    // and slot 1 is reserved for a transient FadeIn at the loop boundary.
    // In width>=2 mode, slots 0..(width-1) form a phase-staggered cluster of
    // continuously-spawned grains.
    struct Grain
    {
        enum class Role { Body, FadeIn, FadeOut };
        bool   active        { false };
        Role   role          { Role::Body };
        float  read_pos      { 0.f };
        float  phase         { 0.f };  // [0, 1) progress through window
        float  phase_inc     { 0.f };  // per-sample phase increment
        bool   spawned_next  { false };
    };

    void prepare_for_trigger(const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq);
    bool check_bounds(float effective_end);
    void retrigger_position();
    void reset_grains();
    void spawn_grain(size_t slot, Grain::Role role, float read_pos, float phase_inc);
    void wrap_grain_read(Grain& g, float start_f, float end_f) const;

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

    // Envelope durations / sustain level (resolved against loop scaling).
    size_t env_attack_{0};
    size_t env_decay_{0};
    size_t env_release_{0};
    float  env_sustain_level_{0.f};

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

    // Granular per-voice params. `base_pitch_` magnitude is editable, sign
    // locked to `forward_` at trigger (like `base_speed_`). window_size_ is
    // in seconds; window_shape_ is the 4-way morph [0,1]; width_ is the
    // grain-cluster size 1..8 (stored as float, snapped at use).
    float  base_pitch_   { 1.f };
    float  window_size_  { 0.5f };
    float  window_shape_ { 0.f };
    float  width_        { 1.f };
    float  sample_rate_  { 48000.f };
    int    last_width_   { 0 };       // detects 1↔N mode flips, forces grain reset

    std::array<Grain, 8> grains_{};

    // Live state
    float        position_{0.f};
    bool         active_{false};
    uint64_t     launch_seq_{0};
    EnvelopeMode envelope_mode_{EnvelopeMode::None};
    idsp::Envelope envelope_{};
};

#endif
