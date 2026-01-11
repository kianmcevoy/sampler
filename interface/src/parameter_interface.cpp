#include "interface/parameter_interface.hpp"
#include "JuceHeader.h"


ParameterInterface::ParameterInterface(ParameterInterfaceOutputData& output)
{

}

void ParameterInterface::load(const ParameterInterfaceLoadData& loaded, ParameterInterfaceOutputData& output)
{

}

void ParameterInterface::process(const ParameterInterfaceInputData& input, ParameterInterfaceOutputData& output)
{
	const float speed_raw = input.controls.sliders.at("speed");
	output.parameter.speed = (speed_raw - 0.5f) * 8.0f; 
	output.parameter.play = input.controls.triggers.at("play");
	output.parameter.stop = input.controls.triggers.at("stop");
	output.parameter.loop = input.controls.buttons.at("loop");
	output.parameter.start = input.controls.sliders.at("start");
	output.parameter.length = input.controls.sliders.at("length");

	if (output.gui.waveform_ready.load() && !output.gui.waveform_left.empty())
	{
		const int num_samples = static_cast<int>(output.gui.waveform_left.size());
		if (num_samples > 0)
		{
			const int start_point = static_cast<int>(input.controls.sliders.at("start") * num_samples);
			const int duration = static_cast<int>(input.controls.sliders.at("length") * num_samples);
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
		// Load the audio file from the path stored in gui_input_data
		const auto file_path = input.gui.sample_file_path;
		if (!file_path.empty())
		{
			juce::File audio_file(file_path);
			if (audio_file.existsAsFile())
			{
				// Create format manager and reader
				juce::AudioFormatManager format_manager;
				format_manager.registerBasicFormats();
				
				std::unique_ptr<juce::AudioFormatReader> reader(format_manager.createReaderFor(audio_file));
				
				if (reader != nullptr)
				{
					// Get sample info
					const int num_channels = static_cast<int>(reader->numChannels);
					const int num_samples = static_cast<int>(reader->lengthInSamples);
					
					// Resize the buffer to fit the sample
					output.buffer.loaded_sample.resize(num_samples);
					
					// Load the audio data into the buffer
					// We'll load stereo (2 channels) - if mono, duplicate to both channels
					if (num_channels == 1)
					{
						// Mono file - load into both channels
						std::vector<float> temp_buffer(num_samples);
						float* channel_ptr = temp_buffer.data();
						reader->read(&channel_ptr, 1, 0, num_samples);
						
						for (int i = 0; i < num_samples; ++i)
						{
							output.buffer.loaded_sample.channel(0)[i] = temp_buffer[i];
							output.buffer.loaded_sample.channel(1)[i] = temp_buffer[i];
						}
					}
					else
					{
						// Stereo or multi-channel file
						std::vector<float> temp_buffer_l(num_samples);
						std::vector<float> temp_buffer_r(num_samples);
						float* channels[2] = { temp_buffer_l.data(), temp_buffer_r.data() };
						
						reader->read(channels, 2, 0, num_samples);
						
						for (int i = 0; i < num_samples; ++i)
						{
							output.buffer.loaded_sample.channel(0)[i] = temp_buffer_l[i];
							output.buffer.loaded_sample.channel(1)[i] = temp_buffer_r[i];
						}
					}
					
					output.buffer.loaded_sample.update();
					
					// Update length to match loaded sample
					// 'end' slider is now a duration/length that adds to start position
					const int start_point = static_cast<int>(input.controls.sliders.at("start") * num_samples);
					const int duration = static_cast<int>(input.controls.sliders.at("length") * num_samples);
					output.gui.start = idsp::clamp(start_point, 0, num_samples - 1);
					output.gui.end = idsp::min(start_point + duration, num_samples);
					
					// Copy sample data to GUI output for waveform display (thread-safe)
					output.gui.waveform_left.resize(num_samples);
					output.gui.waveform_right.resize(num_samples);
					for (int i = 0; i < num_samples; ++i)
					{
						output.gui.waveform_left[i] = output.buffer.loaded_sample.channel(0)[i];
						output.gui.waveform_right[i] = output.buffer.loaded_sample.channel(1)[i];
					}
					output.gui.waveform_ready.store(true);
				}
			}
		}
		
		// Signal to GUI that file has been loaded
		output.gui.file_loaded.store(true);
	}
}
