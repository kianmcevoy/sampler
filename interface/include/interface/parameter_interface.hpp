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
};

#endif
