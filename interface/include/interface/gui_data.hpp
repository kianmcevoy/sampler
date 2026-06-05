#ifndef GUI_DATA_H
#define GUI_DATA_H

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include "instrument/constants.hpp"
#include "interface/voice_param_table.hpp"

/** Atomic mirror of VoiceLiveParams for the audio→GUI display channel.
 *  Indexed by position in voice_param_floats / voice_param_bools so that
 *  adding a new per-voice param is a one-line edit to that table.
 */
struct VoiceParamSnapshot
{
    std::array<std::atomic<float>, voice_param_floats.size()> floats {};
    std::array<std::atomic<bool>,  voice_param_bools.size()>  bools  {};
};

/** Audio → GUI mailbox. All fields are read by the GUI thread and written by
 *  the audio thread; everything is atomic except the bulk `waveform_*` vectors
 *  which are gated by the `waveform_ready` flag.
 */
struct GuiOutputData
{
    // Sample-index markers showing the current playback range on the
    // waveform display (derived from the start + length sliders, or set
    // from a freshly-loaded file). Both are absolute sample positions in
    // the loaded buffer.
    std::atomic<int> display_marker_start { 0 };
    std::atomic<int> display_marker_end   { 0 };

    // Per-voice display data (cursor positions and envelope levels).
    std::array<std::atomic<bool>,  max_voices> voice_active   {};
    std::array<std::atomic<float>, max_voices> voice_position {};
    std::array<std::atomic<float>, max_voices> voice_volume   {};
    std::array<std::atomic<int>,   max_voices> voice_layer    {};

    // Per-layer aggregates for layer-view voice button LEDs. has_active is
    // true iff at least one voice on that layer is alive; summed_envelope is
    // the sum of current envelope×level for those voices.
    std::array<std::atomic<bool>,  max_layers> layer_has_active_voices {};
    std::array<std::atomic<float>, max_layers> layer_summed_envelope   {};

    // Current playback position as a fraction [0, 1] of the active loop
    // region. Drives the bidirectional `position` slider — GUI timer reads
    // this and writes it back to the JUCE param when the user isn't dragging.
    std::atomic<float> playback_position_normalized { 0.f };

    // Per-voice effective live param snapshots (audio→GUI). Updated each
    // block by StateInterface so the GUI can snap sliders on selection.
    std::array<VoiceParamSnapshot, max_voices> voice_params_snapshot;

    // File chooser handshake (audio asks, GUI replies via GuiInputData).
    std::atomic<bool> request_file_chooser { false };
    std::atomic<bool> file_loaded          { false };

    // Waveform data for display. Written by the audio thread during load,
    // read by the GUI thread once `waveform_ready` flips true.
    std::vector<float> waveform_left;
    std::vector<float> waveform_right;
    std::atomic<bool>  waveform_ready { false };

    // Effective marker positions (sample indices) for the currently-enabled
    // marker mode + resolution. ParameterInterface refreshes these each block
    // when markers_enabled is true; marker_count == 0 means "no markers
    // active, GUI should draw nothing". Entries past marker_count are -1.
    std::array<std::atomic<int>, 64> marker_positions {};
    std::atomic<int>                 marker_count     { 0 };

    // Recording state mirror. `is_recording` lights up the REC button while a
    // capture is in progress; `record_progress` is the fraction [0, 1] of the
    // fixed 10 s buffer that has been filled, suitable for driving a progress
    // ring on the REC button. Both are written every block by ParameterInterface.
    std::atomic<bool>  is_recording    { false };
    std::atomic<float> record_progress { 0.f };
};

/** GUI → audio mailbox. Written by the GUI thread (typically from
 *  MainComponent's 100 ms timer), read by the audio thread each block.
 */
struct GuiInputData
{
    // File path for sample to load (set by GUI, consumed by parameter interface)
    std::string       sample_file_path;
    std::atomic<bool> file_path_ready { false };

    // Currently-selected voice button. -1 means "no selection": in Global
    // mode slider edits overlay onto every active voice. 0..max_voices-1
    // means that slot is the live-edit target and the next `play` is forced
    // into that slot.
    std::atomic<int> selected_voice { -1 };

    // Global-mode toggle. Mutually exclusive with selected_voice >= 0 by the
    // GUI radio invariant. When true: slider edits overlay onto every active
    // voice each block; `play` retriggers every active voice; `stop` kills all.
    std::atomic<bool> global_mode { false };

    // True while the user is dragging the `position` slider. Set by the GUI
    // timer from `igui::UIComponent::parameter_being_gestured()`. The audio
    // thread reads this to gate the scrub action: only treat the slider value
    // as a user command while this is true. Avoids the feedback loop where
    // audio's own echoed-back position would otherwise look like user input.
    std::atomic<bool> position_scrubbing { false };

    // Currently selected layer (0..max_layers-1). Written by MainComponent
    // when the user clicks a layer-view button. Drives which sample buffer
    // loads target, which buffer's waveform / markers the GUI displays, and
    // which layer new triggers (play / MIDI / envelope_trigger) tag voices
    // with. Existing voices keep playing their original layer.
    std::atomic<int>  selected_layer { 0 };

    // Layer-view radio: false ⇒ voice view (voice buttons select voices),
    // true ⇒ layer view (voice buttons select layers).
    std::atomic<bool> layer_view { false };

    // Recording control edges. The GUI writes these on REC / STOP / ERASE
    // touch; ParameterInterface consumes them as one-shot edges and clears
    // them at the start of the next block so the GUI doesn't have to manage
    // edge state. The selected layer at the moment of the edge is the target.
    std::atomic<bool> record_start_request { false };
    std::atomic<bool> record_stop_request  { false };
    std::atomic<bool> erase_request        { false };

    // --- Touch trigger queue (single-producer / single-consumer ring) ---
    // The Android touch UI pushes one entry per tap on the waveform. The
    // audio thread drains the queue at the top of each block, converting
    // entries into ParameterData::touch_events that the Instrument launches
    // as voices. write_idx is bumped by the GUI thread, read_idx by the audio
    // thread; both are atomic so the indices serialize access to entries.
    struct PendingTouchEvent
    {
        float start_fraction { 0.f };
        float level          { 1.f };
        int   target_layer   { 0 };
    };
    static constexpr size_t touch_event_queue_size = 16;
    std::array<PendingTouchEvent, touch_event_queue_size> touch_event_queue {};
    std::atomic<uint32_t> touch_event_write_idx { 0 };
    std::atomic<uint32_t> touch_event_read_idx  { 0 };

    // --- Touch scrub state ---
    // While a finger drags an existing playhead, the GUI sets these so the
    // audio thread can route a per-voice scrub. -1 means "no scrub in
    // progress"; otherwise voice_scrub_slot is the voice slot being dragged,
    // voice_scrub_position is the desired loop fraction [0, 1], and
    // voice_scrub_level is the desired level [0, 1]. The scrub follows the
    // existing Voice-mode scrub path inside Instrument.
    std::atomic<int>   voice_scrub_slot     { -1 };
    std::atomic<float> voice_scrub_position { 0.f };
    std::atomic<float> voice_scrub_level    { 1.f };
};

#endif
