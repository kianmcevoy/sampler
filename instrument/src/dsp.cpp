#include "instrument/dsp.hpp"

void Passthrough::process(const PolyDspBuffer& in, PolyDspBuffer& out)
{
    out = in;
}
