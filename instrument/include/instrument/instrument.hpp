#ifndef INSTRUMENT_PARENT_H
#define INSTRUMENT_PARENT_H

#include "instrument/dsp.hpp"
#include "instrument/envelope.hpp"
#include "instrument/state_data.hpp"
#include "system/instrument_data.hpp"
#include "instrument/constants.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>

/** Instrument processor class. */
class Instrument
{
    public:
        Instrument(InstrumentOutputData& output);

        void load(const InstrumentLoadData& loaded, InstrumentOutputData& output);

        void process(const InstrumentInputData& input, InstrumentOutputData& output);

    private:
        // A single playback voice. start_pos / end_pos / length / speed are
        // snapshotted at launch and stay frozen for the playhead's lifetime
        // so slider changes during playback don't disturb in-flight voices.
        //
        // launch_seq is a monotonic counter assigned at launch; the smallest
        // active value is the oldest voice (used by acquire_playhead for
        // voice stealing).
        //
        // envelope_enabled distinguishes how the playhead was launched:
        //   - false (via `play`) — full unity gain.
        //   - true  (via `trigger`/chain) — gain multiplied by envelope.
        // When an enveloped voice's envelope finishes, the slot is freed.
        struct Playhead
        {
            float position;
            size_t start_pos;
            size_t end_pos;
            size_t length;
            float speed;
            // Per-voice level (full-spectrum gain) and pre-computed per-channel
            // pan gains. Snapshot at launch alongside the random_* deviations
            // so a voice's mix position is frozen for its lifetime.
            float level;
            float pan_left;
            float pan_right;
            bool active;
            uint64_t launch_seq;
            idsp::Envelope envelope;
            bool envelope_enabled;
        };

        bool check_bounds(Playhead& ph, bool looping);

        // Returns an inactive slot if one exists, otherwise the active voice
        // with the smallest launch_seq (oldest — voice stealing).
        Playhead& acquire_playhead();

        std::array<Playhead, max_playheads> playheads{};
        uint64_t launch_counter{0};

        // Envelope-chain scheduler state. Armed by parameter.trigger, cancelled
        // by parameter.stop. Each launch within the chain re-reads the current
        // slider values, so changing knobs mid-chain affects later envelopes.
        bool chain_active{false};
        bool chain_looping{false};
        size_t chain_remaining{0};
        int64_t chain_countdown_samples{0};

        // RNG for per-launch parameter jitter (random_speed / random_start /
        // random_length). Seeded once at construction; sampled only on chain
        // launches, not in the per-sample audio loop.
        std::mt19937 rng{std::random_device{}()};
};

#endif
