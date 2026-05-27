#ifndef INSTRUMENT_PARENT_H
#define INSTRUMENT_PARENT_H

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

        // The most recent VoiceLiveParams actually pushed into each voice via
        // set_live_params. voice_live_params_ above stores the trigger-time
        // snapshot (anchor for scaling pickup), so off-layer voices in Global
        // mode would otherwise snap back to that trigger value whenever the
        // user wandered away to another layer. This array remembers what they
        // were last actually playing so they stay there until the user returns
        // and edits them again.
        std::array<VoiceLiveParams, max_voices> voice_effective_params_{};

        // Per-voice slider-axis anchor for Global-mode value-scaling pickup.
        // Captured at every trigger (and re-snapped on Voice-mode override).
        // Paired with the trigger-time effective values in voice_live_params_
        // to form the (slider, voice) pickup point — moving a playback slider
        // in Global mode scales each voice relative to its own anchor.
        std::array<VoiceSliderAnchor, max_voices> voice_anchor_{};

        // Position-scrub anchor for Global-mode value-scaling pickup. Captured
        // on the rising edge of position_scrubbing (the start of a drag),
        // discarded implicitly on the falling edge. The (position_slider_anchor_,
        // voice_position_anchor_[i]) pair defines each voice's pickup point so
        // the scrub scales each voice from where it was at grab-time instead
        // of teleporting every voice to the same slider value.
        std::array<float, max_voices> voice_position_anchor_{};
        float position_slider_anchor_{0.f};
        bool  position_scrubbing_prev_{false};

        // Per-voice MIDI ownership. Non-zero ⇒ the voice is currently held by
        // a MIDI note with that sequence number; zero ⇒ not MIDI-owned (manual
        // play, envelope_trigger, or finished MIDI note). Used to route note-off
        // to the correct voice without depending on slot identity.
        std::array<uint64_t, max_voices> voice_midi_seq_{};

        // Per-voice latch flag. Set by the latch trigger; when true, the next
        // matching note-off is silently swallowed (the voice keeps looping
        // until stop kills it). Cleared when the voice becomes inactive.
        std::array<bool, max_voices> voice_latched_{};
};

#endif
