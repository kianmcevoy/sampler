#ifndef AUDIO_DATA_HPP
#define AUDIO_DATA_HPP

#include "idsp/buffer_types.hpp"
#include "idsp/delay.hpp"
#include "system/buffer.hpp"
#include <array>


struct SampleBuffer
{
	PolyDspBuffer loaded_sample;
	std::array<idsp::LagrangeDelay<524288>, 2> sample;
};

#endif