#ifndef AUDIO_DATA_HPP
#define AUDIO_DATA_HPP

#include "idsp/ringbuffer.hpp"
#include "system/buffer.hpp"
#include <array>


struct SampleBuffer
{
	PolyDspBuffer loaded_sample;
	std::array<idsp::AudioRingBuffer, 2> sample{
		idsp::AudioRingBuffer(loaded_sample.interface()[0]),
		idsp::AudioRingBuffer(loaded_sample.interface()[1])
	};
};

#endif