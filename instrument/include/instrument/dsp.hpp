#ifndef INSTRUMENT_DSP_H
#define INSTRUMENT_DSP_H

#include "system/buffer.hpp"

/** Example passthrough DSP building block. */
class Passthrough
{
    public:
        Passthrough() = default;
        ~Passthrough() = default;

        void process(const PolyDspBuffer& input, PolyDspBuffer& output);
};

#endif
