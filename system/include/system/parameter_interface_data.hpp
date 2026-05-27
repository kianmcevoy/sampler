#ifndef SYSTEM_PARAMETER_INTERFACE_DATA_H
#define SYSTEM_PARAMETER_INTERFACE_DATA_H

#include "instrument/parameter_data.hpp"
#include "instrument/state_data.hpp"
#include "interface/utility_data.hpp"
#include "system/gui_control_data.hpp"
#include "system/osc_control_data.hpp"
#include "interface/gui_data.hpp"
#include "instrument/audio_data.hpp"
#include "instrument/constants.hpp"

#include "JuceHeader.h"

#include <array>

/** Data structure given to ParameterInterface::load as input. */
struct ParameterInterfaceLoadData
{
    const std::vector<const juce::RangedAudioParameter*> parameters;
};

/** Data structure given to ParameterInterface::process as input. */
struct ParameterInterfaceInputData
{
    const GuiControlData& controls;
    const OscInputData& osc;
    const StateData& state;
	const GuiInputData& gui;
	const juce::MidiBuffer& midi;
};

/** Data structure given to ParameterInterface::process, ::load, and constructor
 * as output. */
struct ParameterInterfaceOutputData
{
    ParameterData& parameter;
    UtilityData& utility;
	GuiOutputData& gui;
	// One sample buffer per layer. ParameterInterface loads new audio into
	// layer_buffers[selected_layer] and publishes the selected layer's
	// waveform / markers to the GUI.
	std::array<SampleBuffer, max_layers>& layer_buffers;
};

#endif
