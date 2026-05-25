#ifndef GUI_DATA_H
#define GUI_DATA_H

#include <atomic>
#include <string>
#include <vector>
#include <array>
#include "instrument/constants.hpp"
#include "interface/voice_param_table.hpp"

// Atomic mirror of VoiceLiveParams for the audio→GUI display channel.
// Indexed by position in voice_param_floats / voice_param_bools so that
// adding a new per-voice param is a one-line edit to that table.
struct VoiceParamSnapshot
{
	std::array<std::atomic<float>, voice_param_floats.size()> floats {};
	std::array<std::atomic<bool>,  voice_param_bools.size()>  bools  {};
};

struct GuiOutputData
{
	// Sample-index markers showing the current playback range on the
	// waveform display (derived from the start + length sliders, or set
	// from a freshly-loaded file). Both are absolute sample positions in
	// the loaded buffer.
	std::atomic<int> display_marker_start { 0 };
	std::atomic<int> display_marker_end   { 0 };

	// Per-voice display data (cursor positions and envelope levels).

	std::array<std::atomic<bool>, max_voices>  voice_active;
	std::array<std::atomic<float>, max_voices> voice_position;
	std::array<std::atomic<float>, max_voices> voice_volume;

	// Per-voice effective live param snapshots (audio→GUI). Updated each
	// block by StateInterface so the GUI can snap sliders on selection.
	std::array<VoiceParamSnapshot, max_voices> voice_params_snapshot;

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

	// Currently-selected voice button. -1 means "no selection":
	// in Auto mode (global_mode=false) the audio thread treats slider edits
	// as next-launch params; in Global mode the slider edits overlay onto
	// every active voice. 0..max_voices-1 means that slot is the live-edit
	// target and the next `play` is forced into that slot.
	std::atomic<int> selected_voice { -1 };

	// Global-mode toggle. Mutually exclusive with selected_voice >= 0 by the
	// GUI radio invariant. When true: slider edits overlay onto every active
	// voice each block; `play` retriggers every active voice; `stop` kills all.
	std::atomic<bool> global_mode { false };
};

#endif
