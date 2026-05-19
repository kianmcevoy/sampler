#ifndef INSTRUMENT_PARAMETER_DATA_H
#define INSTRUMENT_PARAMETER_DATA_H

#include "instrument/constants.hpp"

#include <array>
#include <cstddef>

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
};

struct ParameterData
{
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

	// Per-voice live params. When `selected_voice` is in [0, max_voices) the
	// matching slot is the live-edit target — its values are kept current
	// from the GUI and the corresponding Voice reads from it each block.
	// `selected_voice == -1` means "no voice selected" — at play time the
	// allocator picks a slot normally; otherwise the named slot is forced.
	int selected_voice { -1 };
	std::array<VoiceLiveParams, max_voices> voice_live_params {};
};

#endif
