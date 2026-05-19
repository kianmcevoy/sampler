#ifndef INSTRUMENT_PARENT_H
#define INSTRUMENT_PARENT_H

#include "instrument/dsp.hpp"
#include "instrument/parameter_data.hpp"
#include "instrument/state_data.hpp"
#include "instrument/voice.hpp"
#include "instrument/voice_pool.hpp"
#include "system/instrument_data.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

/** Instrument processor: a polyphonic sampler.
 *
 * Owns a fixed-size VoicePool. On each `play` trigger the allocator picks an
 * inactive voice (or steals the oldest if `voice_stealing` is enabled), the
 * Instrument resolves the current parameter snapshot into a Voice::TriggerParams,
 * and the voice runs independently from then on.
 *
 * The sample buffer (a stereo LagrangeDelay pair) is shared across voices —
 * each voice only owns its own playback position and envelope state.
 */
class Instrument
{
    public:
        Instrument(InstrumentOutputData& output);

        void load(const InstrumentLoadData& loaded, InstrumentOutputData& output);

        // Called by the engine on prepareToPlay. Stored for sync-OFF envelope
        // duration calculations (which use an absolute time in seconds).
        void prepare(double sample_rate);

        void process(const InstrumentInputData& input, InstrumentOutputData& output);

    private:
        VoicePool       voices_{};
        VoiceAllocator  allocator_{};
        uint64_t        launch_counter_{0};
        float           sample_rate_{48000.f};
        // xorshift32 state for per-launch random_* jitter. Sampled only on
        // play, not in the audio loop. Lightweight enough for MCU.
        uint32_t        rng_state_{0x12345678u};

        // Per-voice live-editable parameter snapshot. Written at trigger
        // (with random offsets applied), then either kept frozen or updated
        // by the GUI when its slot is the currently selected voice. Each
        // block we push these into the voice via Voice::set_live_params so
        // edits take effect immediately.
        std::array<VoiceLiveParams, max_voices> voice_live_params_{};
};

#endif
