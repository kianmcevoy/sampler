#ifndef INSTRUMENT_VOICE_HPP
#define INSTRUMENT_VOICE_HPP

#include "instrument/envelope.hpp"
#include "instrument/parameter_data.hpp"
#include "idsp/delay.hpp"
#include "idsp/filter.hpp"

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
 *                            release() (MIDI note-on / note-off pair).
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
    void trigger_ahr        (const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq);
    void trigger_ahsr_gated (const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq);

    /** Apply a fresh live-param snapshot without retriggering. */
    void set_live_params(const VoiceLiveParams& p, size_t buffer_size, float sample_rate);

    /** Set the MIDI per-note offsets (pitch in octaves, velocity factor).
     *  Called by Instrument right after a MIDI-triggered ADSR voice spawns.
     *  These offsets live entirely inside Voice — they don't appear in
     *  VoiceLiveParams and are combined with the slider values during
     *  set_live_params (pitch / speed) and per-grain (level). */
    void set_midi_offsets(float octave_offset, float velocity_factor);

    void kill();

    /** Override whether the voice loops at its region end. Trigger functions
     *  install a default (play/MIDI = looping, AR = one-shot); the Instrument
     *  calls this after a trigger to honour the user-facing Loop toggle. */
    void set_sample_loops(bool loops) { sample_loops_ = loops; }

    /** The layer this voice belongs to. The Instrument calls set_layer
     *  immediately after each trigger with the current_layer at the time of
     *  trigger. Voice::process is then driven from layer_buffers[layer()]. */
    void set_layer(int layer_index) { layer_index_ = layer_index; }
    int  layer() const { return layer_index_; }

    /** Begin release. Only meaningful in ADSR mode; no-op otherwise so a
     *  stray MIDI note-off can't disturb a play/envelope-trigger voice. */
    void release();

    bool         is_active()     const { return active_; }
    uint64_t     launch_seq()    const { return launch_seq_; }
    float        position()      const { return position_; }

    /** Teleport the master playhead to `frac` ([0, 1]) of the active loop
     *  region and reset the grain cluster so the jump is immediate. */
    void  set_loop_position_fraction(float frac);
    /** Current master playhead position as a fraction of the loop region. */
    float loop_position_fraction() const;
    EnvelopeMode envelope_mode() const { return envelope_mode_; }
    float        env_value()     const { return envelope_.current_value(); }
    float        env_phase()     const { return envelope_.get_phase(); }
    float        phase()         const;
    /** Current output amplitude (drives voice-button LED brightness). */
    float        current_level() const;

    StereoFrame process(const idsp::LagrangeDelay<524288>& left,
                        const idsp::LagrangeDelay<524288>& right);

private:
    // A single windowed grain. In loop_crossfade_mode_ (timestretch=false)
    // slot 0 holds the Body and slot 1 is a transient FadeIn at the loop
    // boundary. Otherwise the C-OLA cluster fills slots 0..(overlap-1) with
    // continuously spawned grains, each carrying its own per-grain pitch and
    // window selection (random-jittered at spawn time).
    struct Grain
    {
        enum class Role { Body, FadeIn, FadeOut };
        bool   active        { false };
        Role   role          { Role::Body };
        float  read_pos      { 0.f };
        float  pitch_ratio   { 1.f };  // per-sample read advance (per-grain jitter applied)
        float  phase         { 0.f };  // [0, 1) progress through window
        float  phase_inc     { 0.f };  // per-sample window advance
        int    win_lut_a     { 0 };    // window-LUT pair + blend for Kaiser β selection
        int    win_lut_b     { 0 };
        float  win_blend     { 0.f };
        float  level_jit     { 0.f };  // additive per-grain level offset (random_level)
        float  pan_jit       { 0.f };  // additive per-grain pan offset (random_pan)
        // Spawn-time loop boundaries. C-OLA grains wrap against these rather
        // than the live start_pos_/end_pos_ so a start/length scrub doesn't
        // force-wrap mid-window grains and produce a click.
        float  spawn_start   { 0.f };
        float  spawn_end     { 0.f };
    };

    void prepare_for_trigger(const VoiceLiveParams& p, size_t buffer_size, float sample_rate, uint64_t seq);
    bool check_bounds(float effective_end);
    void retrigger_position();
    void reset_grains();
    void spawn_grain_loop_xfade(size_t slot, Grain::Role role, float read_pos, float phase_inc);
    void spawn_cola_grain(size_t slot, float base_read_pos);
    size_t find_grain_slot();
    void wrap_grain_read(Grain& g, float start_f, float end_f) const;
    float wrap_position(float pos, float start_f, float end_f) const;

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
    int    layer_index_{0};  // which layer's sample buffer this voice plays from

    // Envelope durations / sustain level (resolved against loop scaling).
    size_t env_attack_{0};
    size_t env_hold_{0};
    size_t env_decay_{0};
    size_t env_release_{0};
    float  env_sustain_level_{0.f};

    float filter_cutoff_{0.f};
    float filter_resonance_{0.f};

    float depth_speed_{0.f};
    float depth_start_{0.f};
    float depth_length_{0.f};
    float depth_level_{0.f};
    float depth_pan_{0.f};

    float depth_cutoff_{0.f};
    float depth_resonance_{0.f};

    float depth_phase_speed_{0.f};
    float depth_phase_start_{0.f};
    float depth_phase_length_{0.f};
    float depth_phase_level_{0.f};
    float depth_phase_pan_{0.f};

    // User-facing granular deviations (copied from VoiceLiveParams each block).
    float pitch_deviation_   { 0.f };
    float size_deviation_    { 0.f };
    float shape_deviation_   { 0.f };
    float grains_deviation_  { 0.f };
    bool  timestretch_       { false };
    float random_pitch_      { 0.f };
    float random_size_       { 0.f };
    float random_shape_      { 0.f };
    float random_grains_     { 0.f };
    float random_position_   { 0.f };
    float random_level_      { 0.f };
    float random_pan_        { 0.f };

    // MIDI per-note offsets, set by `set_midi_offsets`. Non-MIDI voices
    // (play / envelope_trigger) keep the identity defaults so the same DSP
    // path works uniformly.
    float midi_octave_offset_   { 0.f };  // log2(note_ratio)
    float midi_velocity_factor_ { 1.f };  // (vel/127)^2

    float sample_rate_      { 48000.f };

    // Auto-derived values, recomputed in set_live_params each block.
    float pitch_ratio_         { 1.f };  // 2^pitch_deviation, signed by forward_
    float n_eff_samples_       { 1920.f };
    float window_index_        { 3.f };  // float index into the 7-entry window LUT array
    int   overlap_eff_         { 2 };    // grain-cluster active count
    bool  loop_crossfade_mode_ { false };
    bool  last_loop_crossfade_mode_ { false };  // detects mode flips

    // Synth-side spawning state.
    float    synth_hop_counter_ { 0.f };
    uint32_t grain_rng_         { 0x9E3779B9u };

    std::array<Grain, 8> grains_{};

    // Live state
    float        position_{0.f};
    bool         active_{false};
    uint64_t     launch_seq_{0};
    EnvelopeMode envelope_mode_{EnvelopeMode::None};
    idsp::Envelope envelope_{};

    //Filter
    idsp::SVFilter filter_l;
    idsp::SVFilter filter_r;
};

#endif
