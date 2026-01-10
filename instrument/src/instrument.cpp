#include "instrument/instrument.hpp"

Instrument::Instrument(InstrumentOutputData& output)
{

}

void Instrument::load(const InstrumentLoadData& loaded, InstrumentOutputData& output)
{

}

void Instrument::process(const InstrumentInputData& input, InstrumentOutputData& output)
{
    this->through.process(input.audio, output.audio);
}
