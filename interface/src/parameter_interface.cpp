#include "interface/parameter_interface.hpp"
#include "JuceHeader.h"
#include "idsp/functions.hpp"
#include "system/asset_manager.hpp"


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
	// Sliders return values already in their displayed ranges (configured in
	// gui/src/controls.cpp), so no rescaling is needed here.
	output.parameter.play   = input.controls.triggers.at("play");
	output.parameter.stop   = input.controls.triggers.at("stop");
	output.parameter.loop   = input.controls.buttons.at("loop");
	output.parameter.start  = input.controls.sliders.at("start");
	output.parameter.length = input.controls.sliders.at("length");
	output.parameter.speed  = input.controls.sliders.at("speed");
	output.parameter.level  = input.controls.sliders.at("level");
	output.parameter.pan    = input.controls.sliders.at("pan");

	output.parameter.time    = input.controls.sliders.at("time");
	output.parameter.skew    = input.controls.sliders.at("skew");
	output.parameter.shape   = input.controls.sliders.at("shape");

	output.parameter.loop_envelope =input.controls.buttons.at("loop_envelope");
	output.parameter.voice_stealing = input.controls.buttons.at("voice_stealing");
	output.parameter.envelope_sync = input.controls.buttons.at("envelope_sync");

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

    // Selected voice (GUI-thread state). The Instrument uses this both to
    // route live edits onto a specific voice and to force a `play` into that
    // slot. -1 means "no selection" — fall back to the normal allocator.
    output.parameter.selected_voice = input.gui.selected_voice.load();

	if (output.gui.waveform_ready.load() && !output.gui.waveform_left.empty())
	{
		const int num_samples = static_cast<int>(output.gui.waveform_left.size());
		if (num_samples > 0)
		{
			const int start_point = static_cast<int>(input.controls.sliders.at("start") * num_samples);
			const int duration = idsp::max(static_cast<int>(input.controls.sliders.at("length") * num_samples), 1);
			output.gui.start = idsp::clamp(start_point, 0, num_samples - 1);
			output.gui.end = idsp::min(start_point + duration, num_samples);
		}
	}
	else
	{
		output.gui.start.store(0);
		output.gui.end.store(0);
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
// display waveform buffer. Initialises gui.start / gui.end from the supplied slider positions. Returns true on success.
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
    output.gui.start = idsp::clamp(start_point, 0, num_samples - 1);
    output.gui.end   = idsp::min(start_point + duration, num_samples);

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
