#ifndef AUDIO_DATA_HPP
#define AUDIO_DATA_HPP

#include "idsp/buffer_types.hpp"
#include "idsp/delay.hpp"
#include "system/buffer.hpp"
#include <array>


/** Per-layer sample storage.
 *
 * `loaded_sample` is the canonical waveform — dynamically sized, used by the
 * GUI for display and by onset detection. `sample[]` are the playback buffers
 * (stereo LagrangeDelay pair) the Voice reads from via `read_at(fractional)`.
 * Loading writes both in lockstep; they must stay in sync.
 *
 * `transient_indices` / `transient_count` cache the onset-detection result so
 * marker mode can snap to detected transients without re-running detection
 * every block. `loaded_sample_rate` is the source file's sample rate (used by
 * the onset detector at load time, not by playback).
 */
struct SampleBuffer
{
    PolyDspBuffer loaded_sample;
    std::array<idsp::LagrangeDelay<524288>, 2> sample;

    std::array<int, 64> transient_indices {};
    int                 transient_count { 0 };
    float               loaded_sample_rate { 48000.f };
};

#endif
