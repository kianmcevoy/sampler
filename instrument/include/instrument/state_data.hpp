#ifndef INSTRUMENT_STATE_DATA_H
#define INSTRUMENT_STATE_DATA_H

#include <array>
#include <cstddef>
#include "instrument/constants.hpp"
#include "instrument/parameter_data.hpp"

/** Structure of output/state data for the instrument.
 * Use this to communicate the state of the instrument to the output, i.e. the
 * display processor.
 * Try to avoid thinking about the instrument's state as the same thing as
 * its display state. While the two may be heavily intertwined in the project's
 * current form, this may not be the case if you use this instrument in another
 * project with a different form of UI.
 */
struct StateData
{
	float playback_position { 0.0f }; // Position of the primary (first) active voice, or -1 if none.

	std::array<bool,  max_voices> voice_active   {};
	std::array<float, max_voices> voice_position {};
	std::array<float, max_voices> voice_volume   {};

	// Mirror of the audio thread's per-voice live params. Published every
	// block so the GUI thread can snap its sliders to a voice's effective
	// values when the user selects that voice.
	std::array<VoiceLiveParams, max_voices> voice_live_params {};
};

#endif
