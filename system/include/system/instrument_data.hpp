#ifndef SYSTEM_INSTRUMENT_DATA_H
#define SYSTEM_INSTRUMENT_DATA_H

#include "instrument/parameter_data.hpp"
#include "instrument/state_data.hpp"
#include "instrument/audio_data.hpp"

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
	const SampleBuffer& buffer;
};

/** Data structure given to Instrument::process, ::load, and constructor as
 * input. */
struct InstrumentOutputData
{
    PolyDspBuffer& audio;
    StateData& state;
};

#endif
