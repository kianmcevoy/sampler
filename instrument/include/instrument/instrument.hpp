#ifndef INSTRUMENT_PARENT_H
#define INSTRUMENT_PARENT_H

#include "instrument/dsp.hpp"
#include "system/instrument_data.hpp"

/** Instrument processor class. */
class Instrument
{
    public:
        /** Constructs the Instrument.
         * Use this to initialise any internal data, and state data.
         */
        Instrument(InstrumentOutputData& output);

        /** This method is called on startup, once any autosaved data has been
         * loaded.
         * @note This method is not guaranteed to be called on startup, so
         * should not be relied upon for data initialisation.
         */
        void load(const InstrumentLoadData& loaded, InstrumentOutputData& output);

        /** DSP block processing method. */
        void process(const InstrumentInputData& input, InstrumentOutputData& output);

    private:
        /** Example passthrough DSP block. */
        Passthrough through;
};

#endif
