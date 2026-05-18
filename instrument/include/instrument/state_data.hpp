#ifndef INSTRUMENT_STATE_DATA_H
#define INSTRUMENT_STATE_DATA_H

#include <array>
#include <cstddef>
#include "instrument/constants.hpp"

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
	float playback_position { 0.0f }; // Position of the primary (first) active playhead, or -1 if none.

	std::array<bool,  max_playheads> playhead_active   {};
	std::array<float, max_playheads> playhead_position {};
	std::array<float, max_playheads> playhead_volume   {};
};

#endif
