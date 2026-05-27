#ifndef INSTRUMENT_CONSTANTS_H
#define INSTRUMENT_CONSTANTS_H

#include <cstddef>
static constexpr size_t max_voices = 8;
// Independent sample "layers". The 8 voices are shared globally across
// layers — each voice records which layer it was triggered on and plays
// from that layer's buffer for its lifetime.
static constexpr size_t max_layers = 8;

#endif
