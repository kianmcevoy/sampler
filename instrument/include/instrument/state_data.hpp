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

    // Normalized loop-position fraction [0, 1] of the routing-target voice
    // (Voice mode → selected; Global → first active / newest). Mirrored by
    // StateInterface into GuiOutputData::playback_position_normalized so the
    // bidirectional `position` slider can display it.
    float playback_position_normalized { 0.0f };

    std::array<bool,  max_voices> voice_active   {};
    std::array<float, max_voices> voice_position {};
    std::array<float, max_voices> voice_volume   {};
    std::array<int,   max_voices> voice_layer    {};  // 0..max_layers-1

    // Per-layer aggregates for the layer-view voice button LEDs.
    // layer_has_active_voices[i] is true when at least one voice on layer i is
    // active; layer_summed_envelope[i] is the sum of those voices' current
    // envelope×base levels (clamped to [0, 1] downstream).
    std::array<bool,  max_layers> layer_has_active_voices {};
    std::array<float, max_layers> layer_summed_envelope   {};

    // Mirror of the audio thread's per-voice live params. Published every
    // block so the GUI thread can snap its sliders to a voice's effective
    // values when the user selects that voice.
    std::array<VoiceLiveParams, max_voices> voice_live_params {};
};

#endif
