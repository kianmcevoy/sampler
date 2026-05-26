#ifndef INTERFACE_VOICE_PARAM_TABLE_H
#define INTERFACE_VOICE_PARAM_TABLE_H

#include "instrument/parameter_data.hpp"

#include <array>
#include <cstddef>

// Single source of truth for the per-voice live-editable parameter set.
// Maps each control id (the string declared in gui/src/controls.cpp and read
// in ParameterInterface) to its VoiceLiveParams field. Used by:
//   - GuiOutputData::VoiceParamSnapshot (atomic mirror sizing)
//   - StateInterface::process           (audio → GUI publish loop)
//   - MainComponent::apply_params_to_juce / snapshot_juce_params /
//     read_voice_snapshot                (slider snap on voice select)
//
// To add a new per-voice live-editable parameter:
//   1. Add the field to VoiceLiveParams (instrument/parameter_data.hpp).
//   2. Add a row to the float or bool table below.
//   3. Wire the slider/button in gui/src/controls.cpp and read it in
//      ParameterInterface as usual.
// Everything else (atomic mirror, snapshot, snap-to-voice) follows automatically.
//
// Parameters that are *global* (apply only at next launch — e.g. random_*,
// comp_*) intentionally live ONLY in ParameterData and do NOT belong here.

struct VoiceParamFloatEntry
{
    const char* id;
    float VoiceLiveParams::* field;
};

struct VoiceParamBoolEntry
{
    const char* id;
    bool VoiceLiveParams::* field;
};

inline constexpr std::array<VoiceParamFloatEntry, 23> voice_param_floats = {{
    {"start",            &VoiceLiveParams::start},
    {"length",           &VoiceLiveParams::length},
    {"speed",            &VoiceLiveParams::speed},
    {"level",            &VoiceLiveParams::level},
    {"pan",              &VoiceLiveParams::pan},
    {"attack",           &VoiceLiveParams::attack},
    {"decay",            &VoiceLiveParams::decay},
    {"sustain",          &VoiceLiveParams::sustain},
    {"release",          &VoiceLiveParams::release},
    {"envelope_speed",   &VoiceLiveParams::envelope_speed},
    {"envelope_start",   &VoiceLiveParams::envelope_start},
    {"envelope_length",  &VoiceLiveParams::envelope_length},
    {"envelope_level",   &VoiceLiveParams::envelope_level},
    {"envelope_pan",     &VoiceLiveParams::envelope_pan},
    {"phase_speed",      &VoiceLiveParams::phase_speed},
    {"phase_start",      &VoiceLiveParams::phase_start},
    {"phase_length",     &VoiceLiveParams::phase_length},
    {"phase_level",      &VoiceLiveParams::phase_level},
    {"phase_pan",        &VoiceLiveParams::phase_pan},
    {"pitch",            &VoiceLiveParams::pitch_deviation},
    {"size",             &VoiceLiveParams::size_deviation},
    {"shape",            &VoiceLiveParams::shape_deviation},
    {"grains",           &VoiceLiveParams::grains_deviation},
}};

inline constexpr std::array<VoiceParamBoolEntry, 1> voice_param_bools = {{
    {"timestretch",      &VoiceLiveParams::timestretch},
}};

#endif
