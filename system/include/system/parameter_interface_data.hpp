#ifndef SYSTEM_PARAMETER_INTERFACE_DATA_H
#define SYSTEM_PARAMETER_INTERFACE_DATA_H

#include "instrument/parameter_data.hpp"
#include "instrument/state_data.hpp"
#include "interface/utility_data.hpp"
#include "system/gui_control_data.hpp"
#include "system/osc_control_data.hpp"
#include "system/buffer.hpp"
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
	// Non-const because ParameterInterface clears one-shot edge atomics
	// (record_start_request / record_stop_request / erase_request) here so
	// the GUI thread doesn't have to manage edge state. All access still
	// goes through std::atomic, preserving the existing thread protocol.
	GuiInputData& gui;
	const juce::MidiBuffer& midi;
	// Block-aligned input audio (mono or stereo). Same buffer that's been
	// copied out of JUCE's audio buffer by EngineAudioProcessor::processBlock.
	// Used by the recording path to capture into a layer's SampleBuffer.
	const PolyDspBuffer& audio;
	// Current host sample rate. Needed by the recording path to translate
	// "10 seconds" into a sample-count ceiling. Set by EngineAudioProcessor.
	float sample_rate { 48000.f };
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
