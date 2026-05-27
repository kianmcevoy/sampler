#ifndef SYSTEM_INSTRUMENT_DATA_H
#define SYSTEM_INSTRUMENT_DATA_H

#include "instrument/parameter_data.hpp"
#include "instrument/state_data.hpp"
#include "instrument/audio_data.hpp"
#include "instrument/constants.hpp"

#include <array>

/** Data structure given to Instrument::load as input. */
struct InstrumentLoadData
{
    const ParameterData& parameter;
};

/** Data structure given to the Instrument::process. */
struct InstrumentInputData
{
    const PolyDspBuffer& audio;
    const ParameterData& parameter;
	// One sample buffer per layer. Each voice records its layer index at
	// trigger time and reads from layer_buffers[v.layer()] for its lifetime.
	const std::array<SampleBuffer, max_layers>& layer_buffers;
};

/** Data structure given to Instrument::process, ::load, and constructor as
 * input. */
struct InstrumentOutputData
{
    PolyDspBuffer& audio;
    StateData& state;
};

#endif
