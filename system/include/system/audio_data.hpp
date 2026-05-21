#ifndef SYSTEM_AUDIO_DATA_H
#define SYSTEM_AUDIO_DATA_H

#include "system/buffer.hpp"

/** Data structure for passing audio to the DSP process via shared memory. */
struct AudioData
{
    PolyDspBuffer input;
    PolyDspBuffer output;
};

#endif
