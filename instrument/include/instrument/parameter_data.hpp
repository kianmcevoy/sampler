#ifndef INSTRUMENT_PARAMETER_DATA_H
#define INSTRUMENT_PARAMETER_DATA_H

#include "instrument/constants.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

/** Structure of parameter data for the instrument.
 * The data stored here should be hardware agnostic (as far as is practical) and
 * should be conditioned to the extent that the instrument may use it with no or
 * minimal additional processing.
 */

/** Per-voice live-editable parameter set.
 *
 * When a voice is launched, the audio thread writes the post-random effective
 * values into this struct (so the GUI can snap sliders to "the value this
 * voice is actually playing"). While the voice plays, edits to the selected
 * voice's sliders flow back into this struct, and the voice picks them up
 * each block via Voice::set_live_params(). Voices that aren't currently
 * selected keep whatever was last written here.
 */
struct VoiceLiveParams
{
    float start         { 0.f };
    float length        { 1.f };
    float speed         { 1.f };
    float level         { 1.f };
    float pan           { 0.5f };

    // ADSR envelope: attack/decay/release are durations in seconds (clamped
    // to [0, 5] s by Voice::set_live_params); sustain is a level in [0, 1].
    float attack        { 0.01f };
    float decay         { 0.1f };
    float sustain       { 0.8f };
    float release       { 0.3f };

    // Filter parameters
    float sample_rate_inv { 1.f / 48000.f };
    float filter_freq     { 10000.f };
    float filter_q        { 0.5f };

    // Envelope-driven modulation depths for the five playback params.
    // Bipolar [-1, +1]; the Voice applies them as `depth * envelope_value`.
    float envelope_speed  { 0.f };
    float envelope_start  { 0.f };
    float envelope_length { 0.f };
    float envelope_level  { 0.f };
    float envelope_pan    { 0.f };
    float envelope_cutoff { 0.f };
    float envelope_resonance { 0.f };

    // Playhead-phase modulation depths for the same five playback params.
    // `phase` here means the voice's progress through its loop region in
    // [0, 1], not envelope phase.
    float phase_speed     { 0.f };
    float phase_start     { 0.f };
    float phase_length    { 0.f };
    float phase_level     { 0.f };
    float phase_pan       { 0.f };

    // Granular per-voice params — bipolar deviations from an auto-computed
    // C-OLA optimum (recomputed each block from speed + pitch).
    //   pitch_deviation  : pitch shift in octaves. 0 = no shift (pitch tracks
    //                      speed); ±2 = ±2 octaves.
    //   size_deviation   : log-2 deviation from N_auto. ±1 = ×0.25..×4.
    //   shape_deviation  : window selection. -1 = rect (Kaiser β=0), 0 = Hann-
    //                      like (β=6), +1 = Kaiser β=14.
    //   grains_deviation : -1 → 1 grain, 0 → C-OLA minimum for the chosen β,
    //                      +1 → 8 grains. Piecewise linear.
    float pitch_deviation  { 0.f };
    float size_deviation   { 0.f };
    float shape_deviation  { 0.f };
    float grains_deviation { 0.f };

    // Independent-pitch toggle. ON ⇒ C-OLA granular cluster honors
    // pitch_deviation; OFF ⇒ the voice runs as a single playhead with a
    // loop-boundary crossfade (pitch tracks speed exactly).
    bool  timestretch      { false };

    // Loop-scaled AHSR envelope mode. When true, the envelope_trigger and MIDI
    // note-on fire an AHSR shape whose A/H/R durations are fractions of the
    // active loop's output duration rather than fixed 0–5 s values.
    bool  scale_envelope   { false };

    // Per-grain random depth (0 = identical grains, 0.5 = decorrelating,
    // 1 = spray). Applied inside Voice at each grain spawn.
    float random_pitch    { 0.f };
    float random_size     { 0.f };
    float random_shape    { 0.f };
    float random_grains   { 0.f };
    float random_position { 0.f };  // jitters per-grain starting read position
    float random_level    { 0.f };  // jitters per-grain amplitude
    float random_pan      { 0.f };  // jitters per-grain stereo position
};

/** Per-voice slider-axis anchor for value-scaling pickup.
 *
 * Captured at trigger time alongside the voice's effective live params. While
 * the voice is alive in Global mode, the 5 playback sliders are interpreted
 * relative to this anchor: as the slider moves away from `anchor`, the voice
 * value scales toward the slider's min or max (piecewise linear through
 * (anchor, voice_value)). Returning the slider to `anchor` restores the
 * voice's original (post-random) value; pushing to either extreme collapses
 * all voices onto the slider value.
 */
struct VoiceSliderAnchor
{
    float start  { 0.f };
    float length { 1.f };
    float speed  { 1.f };
    float level  { 1.f };
    float pan    { 0.5f };
};

/** Per-block MIDI note event published by ParameterInterface to Instrument.
 *
 * `midi_seq` is a monotonic counter assigned at note-on; the matching
 * note-off carries the same seq so the Instrument can locate which voice
 * to release without ParameterInterface needing to predict allocator
 * behaviour. velocity/note_ratio are only meaningful for note-on events.
 */
struct MidiNoteEvent
{
    bool     note_on;     // true = note-on (trigger), false = note-off (release)
    uint64_t midi_seq;    // unique per note-on; note-off references the same seq
    float    velocity;    // (vel/127)^2 factor in [0, 1] — Voice multiplies
                          // its per-grain level by this internally (kept out
                          // of VoiceLiveParams so the slider snapshot can't
                          // compound it).
    float    note_ratio;  // pure 2^((note-60)/12) pitch ratio — Voice combines
                          // it with the slider pitch/speed via set_midi_offsets.
    int      note_number; // raw MIDI note in [0, 127]; only meaningful for
                          // note_on events. Consumed by Position routing to
                          // map a note to a marker / start fraction.
};

struct ParameterData
{
    static constexpr size_t max_midi_events_per_block = 32;

    // --- playback controls ---
    float speed;
    float start;
    float length;
    float level;
    float pan;
    bool  play;
    bool  stop;
    bool  latch;
    bool  timestretch;
    bool  loop;               // true ⇒ voices loop indefinitely (default).
                              // false ⇒ voices self-terminate at end of region.
    float position;           // [0, 1] — bidirectional pos-knob (scrub + display)
    bool  position_scrubbing; // true while the user is dragging the position pot

    // --- envelope controls (ADSR) ---
    float attack;
    float decay;
    float sustain;
    float release;
    bool  voice_stealing;
    bool  envelope_trigger;
    bool  scale_envelope;

    // --- filter parameters ---
    float filter_freq     { 10000.f };
    float filter_q        { 0.5f };

    // --- per-launch random jitter (applied once, at trigger time) ---
    float random_speed;
    float random_start;
    float random_length;
    float random_level;
    float random_pan;
    float random_cutoff;
    float random_resonance;

    // --- envelope modulation depths (bipolar [-1, +1]) ---
    float envelope_speed;
    float envelope_start;
    float envelope_length;
    float envelope_level;
    float envelope_pan;
    float envelope_cutoff;
    float envelope_resonance;

    // --- playhead-phase modulation depths (bipolar [-1, +1]) ---
    float phase_speed;
    float phase_start;
    float phase_length;
    float phase_level;
    float phase_pan;

    // --- granular per-voice deviations (mirrors VoiceLiveParams) ---
    float pitch_deviation;
    float size_deviation;
    float shape_deviation;
    float grains_deviation;

    // --- granular per-grain random depths (0 = none, 1 = spray) ---
    float random_pitch;
    float random_size;
    float random_shape;
    float random_grains;
    float random_position;

    // Per-voice live params. When `selected_voice` is in [0, max_voices) the
    // matching slot is the live-edit target — its values are kept current
    // from the GUI and the corresponding Voice reads from it each block.
    // `selected_voice == -1` means "no voice selected" — at play time the
    // allocator picks a slot normally; otherwise the named slot is forced.
    int selected_voice { -1 };

    // Global mode: slider edits overlay onto every active voice each block,
    // `play` retriggers every active voice in unison, `stop` kills all.
    // Mutually exclusive with selected_voice >= 0 (GUI radio invariant).
    bool global_mode { false };

    // Markers: when enabled, start/length are snapped by ParameterInterface
    // to discrete marker positions before reaching the instrument. marker_type
    // 0 = time (evenly spaced grid), 1 = transient (from onset detection).
    // resolution selects how many markers (1..64) are active.
    bool markers_enabled { false };
    int  marker_type     { 0 };
    int  resolution      { 8 };

    // MIDI note routing. 0 = Pitch (note shifts speed by 2^((note-60)/12) as
    // before). 1 = Position (note picks a start fraction / marker; pitch is
    // left untouched). Consumed by the Instrument's MIDI note-on handler.
    int  note_route_mode { 0 };

    // Currently selected layer (0..max_layers-1). New triggers tag voices
    // with this index; the instrument routes each voice's process call to
    // layer_buffers[v.layer()] regardless of the current value, so existing
    // voices keep playing their original layer when the user switches.
    int  current_layer { 0 };

    // Kill every active voice on every layer (vs the per-layer `stop`).
    // Fires once per click via the standard trigger semantics.
    bool stop_all { false };

    // Effective marker context for the instrument, refreshed each block.
    // marker_count is the actual N in use (transient may be less than
    // resolution if the sample has fewer onsets). start_marker is the index
    // the slider snapped to; length_markers is the span (1..N-start_marker).
    // marker_fractions[0..marker_count) are the marker sample positions as
    // [0, 1] fractions of buffer length. Used by the instrument to quantise
    // per-launch random jitter onto marker positions.
    int                   marker_count    { 0 };
    int                   start_marker    { 0 };
    int                   length_markers  { 1 };
    std::array<float, 64> marker_fractions {};

    std::array<VoiceLiveParams, max_voices> voice_live_params {};

    // Per-block MIDI events. ParameterInterface writes; Instrument consumes.
    // Both run on the audio thread, so no atomics needed. Excess events past
    // max_midi_events_per_block are dropped at the source.
    std::array<MidiNoteEvent, max_midi_events_per_block> midi_events {};
    size_t midi_event_count { 0 };
};

#endif
