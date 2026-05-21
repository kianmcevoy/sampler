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
enum class Mode
{
	OneShot,
	Looping
};

enum class ComparatorSource
{
	None,
	LoopPhase,
	EnvPhase
};

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
	bool  loop          { false };

	float time          { 1.f };
	float skew          { 0.5f };
	float shape         { 0.f };
	bool  loop_envelope { false };
	bool  envelope_sync { false };

	float envelope_speed  { 0.f };
	float envelope_start  { 0.f };
	float envelope_length { 0.f };
	float envelope_level  { 0.f };
	float envelope_pan    { 0.f };

	float phase_speed     { 0.f };
	float phase_start     { 0.f };
	float phase_length    { 0.f };
	float phase_level     { 0.f };
	float phase_pan       { 0.f };
};

/** Per-block MIDI note event published by ParameterInterface to Instrument.
 *
 * `midi_seq` is a monotonic counter assigned at note-on; the matching
 * note-off carries the same seq so the Instrument can locate which voice
 * to release without ParameterInterface needing to predict allocator
 * behaviour. velocity/speed_ratio are only meaningful for note-on events.
 */
struct MidiNoteEvent
{
	bool     note_on;     // true = note-on (trigger), false = note-off (release)
	uint64_t midi_seq;    // unique per note-on; note-off references the same seq
	float    velocity;    // level (slider_level * (vel/127)^2) — note-on only
	float    speed_ratio; // slider_speed * 2^((note-60)/12) — note-on only
};

struct ParameterData
{
	static constexpr size_t max_midi_events_per_block = 32;

	//playback controls
	float speed;
	float start;
	float length;
	float level;
	float pan;
	bool play;
	bool stop;
	bool loop;

	//envelope controls
	float time;
	float skew;
	float shape;
	bool loop_envelope;
    bool voice_stealing;
    bool envelope_sync;
    bool envelope_trigger;

    //comparator
    ComparatorSource comp_source;       // 0 = None, 1 = loop phase, 2 = env phase
    float  comp_threshold;

	//random modulation
	float random_speed;
	float random_start;
	float random_length;
	float random_level;
	float random_pan;

    //envelope modulation
	float envelope_speed;
	float envelope_start;
	float envelope_length;
	float envelope_level;
	float envelope_pan;

    //phase (per-voice playhead) modulation
	float phase_speed;
	float phase_start;
	float phase_length;
	float phase_level;
	float phase_pan;

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

	std::array<VoiceLiveParams, max_voices> voice_live_params {};

	// Per-block MIDI events. ParameterInterface writes; Instrument consumes.
	// Both run on the audio thread, so no atomics needed. Excess events past
	// max_midi_events_per_block are dropped at the source.
	std::array<MidiNoteEvent, max_midi_events_per_block> midi_events {};
	size_t midi_event_count { 0 };
};

#endif
