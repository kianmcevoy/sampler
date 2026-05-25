#include "interface/parameter_interface.hpp"
#include "JuceHeader.h"
#include "idsp/functions.hpp"
#include "system/asset_manager.hpp"

#include <cmath>


ParameterInterface::ParameterInterface(ParameterInterfaceOutputData& output)
{
	// Pre-load the bundled default sample so the sampler is usable without
	// going through the file chooser first. Start / length default to the
	// full file.
	const auto default_sample = AssetManager::get_resource_file("gui/assets/sample.wav");
	if (load_sample_into_buffer(default_sample, output, 0.0f, 1.0f))
	{
		// Trigger a GUI repaint of the waveform once the panels come up.
		output.gui.file_loaded.store(true);
	}
}

void ParameterInterface::load(const ParameterInterfaceLoadData& loaded, ParameterInterfaceOutputData& output)
{

}

void ParameterInterface::process(const ParameterInterfaceInputData& input, ParameterInterfaceOutputData& output)
{
	// Parse MIDI first so velocity/pitch reads use the current slider values
	// captured *before* any modwheel-driven start offset is applied below.
	this->process_midi(input.midi, input.controls, output.parameter);

	// Sliders return values already in their displayed ranges (configured in
	// gui/src/controls.cpp), so no rescaling is needed here.
	output.parameter.play        = input.controls.triggers.at("play");
	output.parameter.stop        = input.controls.triggers.at("stop");
	output.parameter.loop        = input.controls.buttons.at("loop");
	output.parameter.timestretch = input.controls.buttons.at("timestretch");
	// Modwheel sums with the start slider, clamped to [0, 1].
	output.parameter.start  = idsp::clamp(input.controls.sliders.at("start") + this->modwheel_position_, 0.f, 1.f);
	output.parameter.length = input.controls.sliders.at("length");
	output.parameter.speed  = input.controls.sliders.at("speed");
	output.parameter.level  = input.controls.sliders.at("level");
	output.parameter.pan    = input.controls.sliders.at("pan");

	output.parameter.attack  = input.controls.sliders.at("attack");
	output.parameter.decay   = input.controls.sliders.at("decay");
	output.parameter.sustain = input.controls.sliders.at("sustain");
	output.parameter.release = input.controls.sliders.at("release");

	output.parameter.voice_stealing = input.controls.buttons.at("voice_stealing");
	output.parameter.envelope_trigger = input.controls.triggers.at("envelope_trigger");

	output.parameter.random_speed = input.controls.sliders.at("random_speed");
	output.parameter.random_start = input.controls.sliders.at("random_start");
	output.parameter.random_length = input.controls.sliders.at("random_length");
	output.parameter.random_level = input.controls.sliders.at("random_level");
	output.parameter.random_pan = input.controls.sliders.at("random_pan");

    output.parameter.envelope_speed = input.controls.sliders.at("envelope_speed");
    output.parameter.envelope_start = input.controls.sliders.at("envelope_start");
    output.parameter.envelope_length = input.controls.sliders.at("envelope_length");
    output.parameter.envelope_level = input.controls.sliders.at("envelope_level");
    output.parameter.envelope_pan = input.controls.sliders.at("envelope_pan");

    output.parameter.phase_speed  = input.controls.sliders.at("phase_speed");
    output.parameter.phase_start  = input.controls.sliders.at("phase_start");
    output.parameter.phase_length = input.controls.sliders.at("phase_length");
    output.parameter.phase_level  = input.controls.sliders.at("phase_level");
    output.parameter.phase_pan    = input.controls.sliders.at("phase_pan");

    output.parameter.pitch        = input.controls.sliders.at("pitch");
    output.parameter.window_size  = input.controls.sliders.at("window_size");
    output.parameter.window_shape = input.controls.sliders.at("window_shape");
    output.parameter.width        = input.controls.sliders.at("width");

    output.parameter.random_pitch        = input.controls.sliders.at("random_pitch");
    output.parameter.random_window_size  = input.controls.sliders.at("random_window_size");
    output.parameter.random_window_shape = input.controls.sliders.at("random_window_shape");
    output.parameter.random_width        = input.controls.sliders.at("random_width");

    // Selected voice (GUI-thread state). The Instrument uses this both to
    // route live edits onto a specific voice and to force a `play` into that
    // slot. -1 means "no selection" — fall back to the normal allocator.
    output.parameter.selected_voice = input.gui.selected_voice.load();
    output.parameter.global_mode    = input.gui.global_mode.load();

	if (output.gui.waveform_ready.load() && !output.gui.waveform_left.empty())
	{
		const int num_samples = static_cast<int>(output.gui.waveform_left.size());
		if (num_samples > 0)
		{
			const int start_point = static_cast<int>(input.controls.sliders.at("start") * num_samples);
			const int duration = idsp::max(static_cast<int>(input.controls.sliders.at("length") * num_samples), 1);
			output.gui.display_marker_start = idsp::clamp(start_point, 0, num_samples - 1);
			output.gui.display_marker_end   = idsp::min(start_point + duration, num_samples);
		}
	}
	else
	{
		output.gui.display_marker_start.store(0);
		output.gui.display_marker_end.store(0);
	}


	// When load_sample trigger is pressed, request file chooser from GUI
	if (input.controls.triggers.at("load_sample"))
	{
		output.gui.request_file_chooser.store(true);
	}

	// Check if a file has been selected and is ready to load
	if (input.gui.file_path_ready.load())
	{
		const auto& file_path = input.gui.sample_file_path;
		if (!file_path.empty())
		{
			this->load_sample_into_buffer(juce::File(file_path), output,
			                        input.controls.sliders.at("start"),
			                        input.controls.sliders.at("length"));
		}

		// Always acknowledge the handshake — GUI clears file_path_ready on this.
		output.gui.file_loaded.store(true);
	}
}

// Load the audio file at `audio_file` into both the playback delay lines and
// display waveform buffer. Initialises gui.display_marker_start / _end from the supplied slider positions. Returns true on success.
bool ParameterInterface::load_sample_into_buffer(const juce::File& audio_file, ParameterInterfaceOutputData& output, float start_slider, float length_slider)
{
    if (!audio_file.existsAsFile()) return false;

    juce::AudioFormatManager format_manager;
    format_manager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(format_manager.createReaderFor(audio_file));
    if (reader == nullptr) return false;

    const int num_channels = static_cast<int>(reader->numChannels);
    // LagrangeDelay storage is statically sized; cap num_samples to its capacity.
    constexpr int max_samples = 524288;
    const int num_samples = idsp::min(static_cast<int>(reader->lengthInSamples), max_samples);

    output.buffer.loaded_sample.resize(num_samples);
    output.buffer.sample[0].reset();
    output.buffer.sample[1].reset();

    if (num_channels == 1)
    {
        std::vector<float> temp_buffer(num_samples);
        float* channel_ptr = temp_buffer.data();
        reader->read(&channel_ptr, 1, 0, num_samples);

        for (int i = 0; i < num_samples; ++i)
        {
            output.buffer.loaded_sample.channel(0)[i] = temp_buffer[i];
            output.buffer.loaded_sample.channel(1)[i] = temp_buffer[i];
            output.buffer.sample[0].write(temp_buffer[i]);
            output.buffer.sample[1].write(temp_buffer[i]);
        }
    }
    else
    {
        std::vector<float> temp_buffer_l(num_samples);
        std::vector<float> temp_buffer_r(num_samples);
        float* channels[2] = { temp_buffer_l.data(), temp_buffer_r.data() };

        reader->read(channels, 2, 0, num_samples);

        for (int i = 0; i < num_samples; ++i)
        {
            output.buffer.loaded_sample.channel(0)[i] = temp_buffer_l[i];
            output.buffer.loaded_sample.channel(1)[i] = temp_buffer_r[i];
            output.buffer.sample[0].write(temp_buffer_l[i]);
            output.buffer.sample[1].write(temp_buffer_r[i]);
        }
    }

    output.buffer.loaded_sample.update();

    const int start_point = static_cast<int>(start_slider * num_samples);
    const int duration    = idsp::max(static_cast<int>(length_slider * num_samples), 1);
    output.gui.display_marker_start = idsp::clamp(start_point, 0, num_samples - 1);
    output.gui.display_marker_end   = idsp::min(start_point + duration, num_samples);

    output.gui.waveform_left.resize(num_samples);
    output.gui.waveform_right.resize(num_samples);
    for (int i = 0; i < num_samples; ++i)
    {
        output.gui.waveform_left[i]  = output.buffer.loaded_sample.channel(0)[i];
        output.gui.waveform_right[i] = output.buffer.loaded_sample.channel(1)[i];
    }
    output.gui.waveform_ready.store(true);
    return true;
}

void ParameterInterface::process_midi(const juce::MidiBuffer& midi,
                                      const GuiControlData& controls,
                                      ParameterData& out)
{
    out.midi_event_count = 0;

    const float slider_level   = controls.sliders.at("level");
    const bool  voice_stealing = controls.buttons.at("voice_stealing");

    // Push raw MIDI bytes through the parser. JUCE delivers each message as
    // a contiguous block already framed by status byte; the imidi queue
    // handles running-status and multi-byte streams internally.
    for (const auto meta : midi)
    {
        this->midi_queue_.from_bytes(meta.data, static_cast<size_t>(meta.numBytes));
    }

    while (this->midi_queue_.num_messages() > 0
        && out.midi_event_count < ParameterData::max_midi_events_per_block)
    {
        const imidi::Message msg = this->midi_queue_.read_message();
        const auto t = msg.type();

        if (t == imidi::MessageType::NoteOn)
        {
            const auto d = msg.data_as<imidi::Message::NoteOn>();
            if (d.velocity == 0)
            {
                // MIDI running-status convention: NoteOn velocity 0 == NoteOff.
                this->emit_note_off(d.note, out);
                continue;
            }

            const int slot = this->allocate_midi_voice_slot(voice_stealing);
            if (slot < 0) continue;   // all slots busy, stealing off — drop

            const uint64_t seq = ++this->midi_seq_counter_;
            this->active_notes_[slot] = ActiveNote { /*active=*/true, /*note=*/d.note, /*midi_seq=*/seq };

            const float v01 = static_cast<float>(d.velocity) / 127.f;
            const float level_scaled = slider_level * v01 * v01;
            // Pure pitch ratio (no slider speed baked in) — the Instrument
            // applies it to either pitch or speed depending on timestretch.
            const float ratio = std::pow(2.f, (static_cast<float>(d.note) - 60.f) / 12.f);

            out.midi_events[out.midi_event_count++] = MidiNoteEvent {
                /*note_on=*/true,
                /*midi_seq=*/seq,
                /*velocity=*/level_scaled,
                /*note_ratio=*/ratio,
            };
        }
        else if (t == imidi::MessageType::NoteOff)
        {
            const auto d = msg.data_as<imidi::Message::NoteOff>();
            this->emit_note_off(d.note, out);
        }
        else if (t == imidi::MessageType::ControlChange)
        {
            const auto d = msg.data_as<imidi::Message::ControlChange>();
            if (d.controller == 1)
            {
                // CC1 (modwheel) — latched, persists across blocks.
                this->modwheel_position_ = static_cast<float>(d.value) / 127.f;
            }
        }
        // PitchBend / ProgramChange / etc — ignored for now.
    }
}

void ParameterInterface::emit_note_off(uint8_t note, ParameterData& out)
{
    for (size_t s = 0; s < this->active_notes_.size(); ++s)
    {
        auto& slot = this->active_notes_[s];
        if (slot.active && slot.note == note)
        {
            if (out.midi_event_count < ParameterData::max_midi_events_per_block)
            {
                out.midi_events[out.midi_event_count++] = MidiNoteEvent {
                    /*note_on=*/false,
                    /*midi_seq=*/slot.midi_seq,
                    /*velocity=*/0.f,
                    /*note_ratio=*/0.f,
                };
            }
            slot = ActiveNote{};   // clear
            return;                // one note-off → one voice release
        }
    }
}

int ParameterInterface::allocate_midi_voice_slot(bool voice_stealing)
{
    // First inactive slot wins.
    for (size_t s = 0; s < this->active_notes_.size(); ++s)
    {
        if (!this->active_notes_[s].active) return static_cast<int>(s);
    }
    // All slots full. The audio-side VoiceAllocator picks the actual voice
    // to steal by launch_seq, so any slot here is fine — we just need *a*
    // mapping for note-off matching, and overwriting an existing slot means
    // the previously-stolen note's note-off becomes a no-op (the voice it
    // referred to is already gone). Pick slot 0 for determinism.
    if (voice_stealing) return 0;
    return -1;
}
