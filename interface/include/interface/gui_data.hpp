#ifndef GUI_DATA_H
#define GUI_DATA_H

#include <atomic>
#include <string>
#include <vector>
#include <array>
#include "instrument/constants.hpp"

// Atomic mirror of VoiceLiveParams for the audio→GUI display channel. The
// GUI samples these when the user selects a voice, to snap the sliders to
// that voice's current effective values.
struct VoiceParamSnapshot
{
	std::atomic<float> start          { 0.f };
	std::atomic<float> length         { 1.f };
	std::atomic<float> speed          { 1.f };
	std::atomic<float> level          { 1.f };
	std::atomic<float> pan            { 0.5f };
	std::atomic<bool>  loop           { false };

	std::atomic<float> time           { 1.f };
	std::atomic<float> skew           { 0.5f };
	std::atomic<float> shape          { 0.f };
	std::atomic<bool>  loop_envelope  { false };
	std::atomic<bool>  envelope_sync  { false };

	std::atomic<float> envelope_speed  { 0.f };
	std::atomic<float> envelope_start  { 0.f };
	std::atomic<float> envelope_length { 0.f };
	std::atomic<float> envelope_level  { 0.f };
	std::atomic<float> envelope_pan    { 0.f };
};

struct GuiOutputData
{
	std::atomic<int> start { 0 };
	std::atomic<int> end { 69 };

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

	// Currently-selected voice button. -1 means "no selection" (global mode):
	// the audio thread treats slider edits as next-launch params. 0..max_voices-1
	// means that slot is the live-edit target and the next `play` is forced
	// into that slot.
	std::atomic<int> selected_voice { -1 };
};

#endif
