#ifndef PARAMETER_INTERFACE_H
#define PARAMETER_INTERFACE_H

#include "system/parameter_interface_data.hpp"
#include "imidi/message.hpp"
#include "imidi/queue.hpp"

#include <array>
#include <cstdint>

/** This class is used for translating hardware controls and other means of
 * control input (e.g. MIDI) into parameters used by the Instrument.
 */
class ParameterInterface
{
    public:
        /** Use the constructor to initialise any interface-internal data, and
         * to initialise parameter and utility data.
         */
        ParameterInterface(ParameterInterfaceOutputData& output);
        ~ParameterInterface() = default;

        /** This method is called on startup, once any autosaved data has been
         * loaded.
         * @note This method is not guaranteed to be called on startup, so
         * should not be relied upon for data initialisation.
         */
        void load(const ParameterInterfaceLoadData& loaded, ParameterInterfaceOutputData& output);

        /** DSP loop processor.
         * Use this to translate control data into instrument parameters. This
         * includes UI features, such as button combinations.
         */
        void process(const ParameterInterfaceInputData& input, ParameterInterfaceOutputData& output);

    private:
        // Load `audio_file` into `target` (one layer's buffer) and publish the
        // resulting waveform + display markers to `gui`. Used both by the
        // constructor (loads layer 0's default sample) and by the file-chooser
        // handshake (loads into the currently-selected layer's buffer).
        bool load_sample_into_buffer(const juce::File& audio_file,
                                     SampleBuffer& target, GuiOutputData& gui,
                                     float start_slider, float length_slider);

        // Republish `target`'s waveform + display markers into `gui` without
        // re-decoding the audio. Called when the user switches layer so the
        // display swaps to the newly-selected layer's sample.
        void publish_waveform(const SampleBuffer& target, GuiOutputData& gui,
                              float start_slider, float length_slider);

        // --- Recording ---
        // Arm a 10-second capture into layer_buffers[target_layer]. Zeros the
        // target buffer, kills any voices currently playing on it, and seeds
        // recording state. The next blocks will copy `audio` into the buffer
        // until the 10 s ceiling is hit or stop_recording() is called.
        void start_recording(int target_layer, float sample_rate,
                             std::array<SampleBuffer, max_layers>& layer_buffers,
                             ParameterData& p);

        // End an active capture: re-run onset detection on the new contents,
        // republish the waveform if the recorded layer is the displayed one,
        // and clear recording state. Safe to call when no capture is active.
        void stop_recording(std::array<SampleBuffer, max_layers>& layer_buffers,
                            GuiOutputData& gui,
                            float start_slider, float length_slider);

        // Per-block capture step. While recording_layer_ >= 0, copy this
        // block's input audio into recording_layer_'s SampleBuffer (both
        // loaded_sample and the LagrangeDelay playback pair), advancing
        // recording_sample_pos_. Auto-stops at max_record_samples_.
        void process_recording(const PolyDspBuffer& audio,
                               std::array<SampleBuffer, max_layers>& layer_buffers,
                               GuiOutputData& gui,
                               float start_slider, float length_slider);

        // Zero a layer's SampleBuffer (both display + playback pair),
        // clear its transient cache, and republish if it's the displayed
        // layer. Used by the ERASE trigger.
        void erase_layer_buffer(int layer_index,
                                std::array<SampleBuffer, max_layers>& layer_buffers,
                                GuiOutputData& gui);

        // --- MIDI ---
        struct ActiveNote
        {
            bool     active   { false };
            uint8_t  note     { 0 };
            uint64_t midi_seq { 0 };
        };

        // Parse the MIDI buffer for this block, drive the note-stack, and
        // emit MidiNoteEvents onto `out.midi_events`. Also updates
        // `modwheel_position_` from CC1.
        void process_midi(const juce::MidiBuffer& midi, const GuiControlData& controls, ParameterData& out);

        // Find the active-note slot for `note` and emit a note-off event
        // referencing its midi_seq; clears the slot.
        void emit_note_off(uint8_t note, ParameterData& out);

        // Return a slot in active_notes_ to use for a new note-on, or -1 if
        // all slots are full and stealing is disabled.
        int  allocate_midi_voice_slot(bool voice_stealing);

        // One entry per *held* MIDI note. Indexed by an internal slot, not
        // by note number. Sized to max_voices so we can never hold more
        // notes than we have voices.
        std::array<ActiveNote, max_voices> active_notes_ {};

        // Monotonic counter assigned to each note-on so note-offs can route
        // by sequence rather than by slot (the audio-side voice allocator is
        // free to put the voice anywhere — slot identity is not preserved).
        uint64_t midi_seq_counter_ { 0 };

        // Latched CC1 (modwheel) value in [0, 1]. Persists across blocks.
        float modwheel_position_ { 0.f };

        // The layer currently published into the GUI waveform display. When
        // GuiInputData::selected_layer changes, the next process() re-publishes
        // the new layer's waveform and updates this latch.
        int last_published_layer_ { 0 };

        // Per-block parse buffer. 256 messages is well over a typical block.
        imidi::MessageQueueStatic<256> midi_queue_ {};

        // --- Recording state ---
        // -1 ⇒ idle. 0..max_layers-1 ⇒ capturing into that layer's SampleBuffer.
        int recording_layer_ { -1 };

        // Sample-count progress into the recording buffer (per-channel; mono +
        // stereo write the same number of samples per block). Reset to 0 on
        // start_recording, hits max_record_samples_ at the auto-stop ceiling.
        int recording_sample_pos_ { 0 };

        // 10 seconds at the current sample rate. Established in prepare()
        // via the first start_recording, cached for fast comparisons.
        int max_record_samples_ { 480000 }; // safe default ≈ 10 s @ 48 kHz

        // Cached sample rate for converting "10 seconds" into a sample ceiling.
        // Updated each block from input.audio's expectations.
        float sample_rate_ { 48000.f };
};

#endif
