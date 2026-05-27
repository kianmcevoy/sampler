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

	// Onset-detected transient sample indices, populated at load time by
	// ParameterInterface and consumed in marker mode. Sorted ascending by
	// time, top-64 by detected strength. transient_count == 0 means no
	// onsets were found (or no sample is loaded).
	std::array<int, 64> transient_indices{};
	int                 transient_count{0};
	float               loaded_sample_rate{48000.f};
};

#endif