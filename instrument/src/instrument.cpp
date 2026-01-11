#include "instrument/instrument.hpp"

Instrument::Instrument(InstrumentOutputData& output) : 
position{0.0f},
playing{false}
{

}

void Instrument::load(const InstrumentLoadData& loaded, InstrumentOutputData& output)
{

}

void Instrument::process(const InstrumentInputData& input, InstrumentOutputData& output)
{
	const size_t block_size = output.audio.channel(0).size();
	const size_t buffer_size = input.buffer.loaded_sample.channel(0).size();
	if(buffer_size == 0)
	{
		return;
	}

	const size_t start_idx = static_cast<size_t>(input.parameter.start * buffer_size);
	const size_t duration_samples = static_cast<size_t>(input.parameter.length * buffer_size);
	const size_t end_idx = idsp::min(start_idx + duration_samples, buffer_size);
	
	if (input.parameter.play)
	{
		playing = true;
		if(input.parameter.speed >= 0.0f)
			position = static_cast<float>(start_idx);
		else
			position = static_cast<float>(end_idx - 1);
	}

	if (input.parameter.stop)
	{
		playing = false;
		position = static_cast<float>(start_idx);
	}
	
	if (playing)
	{
		const float end_idx_float = static_cast<float>(end_idx);
		const float start_idx_float = static_cast<float>(start_idx);
		const float duration_float = static_cast<float>(duration_samples);
		
		const auto& left_buffer = input.buffer.sample[0];
		const auto& right_buffer = input.buffer.sample[1];
		
		for (size_t sample_idx = 0; sample_idx < block_size; ++sample_idx)
		{
			if (position >= end_idx_float)
			{
				if (input.parameter.loop)
				{
					while (position >= end_idx_float && duration_float > 0)
					{
						position -= duration_float;
					}

					if (position < start_idx_float)
					{
						position = start_idx_float;
					}
				}
				else
				{
					playing = false;
					position = end_idx_float - 1.0f;			
					break;
				}
			}
			else if (position < start_idx_float)
			{
				if (input.parameter.loop)
				{
					while (position < start_idx_float && duration_float > 0)
					{
						position += duration_float;
					}
					if (position >= end_idx_float)
					{
						position = end_idx_float - 1.0f;
					}
				}
				else
				{
					playing = false;
					position = end_idx_float - 1.0f;
					break;
				}
			}
			
			output.audio.channel(0)[sample_idx] = left_buffer.read_at_smooth_safe(position);
			output.audio.channel(1)[sample_idx] = right_buffer.read_at_smooth_safe(position);
			
	
			position += input.parameter.speed;
		}
	}

	if(!playing)
	{
		for (size_t i = 0; i < block_size; ++i)
		{
			output.audio.channel(0)[i] = 0.0f;
			output.audio.channel(1)[i] = 0.0f;
		}
	}
	
	output.state.playback_position = position;
}
