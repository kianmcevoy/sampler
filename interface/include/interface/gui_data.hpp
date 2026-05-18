#ifndef GUI_DATA_H
#define GUI_DATA_H

#include <atomic>
#include <string>
#include <vector>
#include <array>
#include "instrument/constants.hpp"

struct GuiOutputData
{
	std::atomic<int> start { 0 };
	std::atomic<int> end { 69 };

	// Playhead display data (positions and volumes for active playheads)

	std::array<std::atomic<bool>, max_playheads> playhead_active;
	std::array<std::atomic<float>, max_playheads> playhead_position;
	std::array<std::atomic<float>, max_playheads> playhead_volume;

	// File chooser request (set by parameter interface, consumed by GUI)
	std::atomic<bool> request_file_chooser { false };
	// File loaded acknowledgment (set by parameter interface, consumed by GUI)
	std::atomic<bool> file_loaded { false };

	// Waveform data for display (written by audio thread, read by GUI)
	std::vector<float> waveform_left;
	std::vector<float> waveform_right;
	std::atomic<bool> waveform_ready { false };
};

struct GuiInputData
{
	// File path for sample to load (set by GUI, consumed by parameter interface)
	std::string sample_file_path;
	std::atomic<bool> file_path_ready { false };
};

#endif