#ifndef INTERFACE_MODE_CONTROLLER_H
#define INTERFACE_MODE_CONTROLLER_H

#include "instrument/parameter_data.hpp"

#include <cstddef>
#include <functional>

struct GuiInputData;
struct GuiOutputData;

/** Tracks the Voice / Global radio state for the sampler.
 *
 * Lives in interface/ (not system/) because it knows about both GUI
 * concerns (the global JUCE toggle, selected_voice publishing) and
 * instrument concerns (per-voice VoiceLiveParams snap/restore). It does
 * NOT know about JUCE directly — MainComponent feeds it JUCE-param
 * snapshot/apply through callbacks at construction time.
 *
 * The mode machine:
 *   - Global  : default. No voice selected. Slider edits overlay all active
 *               voices; play retriggers all; stop kills all.
 *   - Voice N : voice N selected. Slider edits overlay only that voice;
 *               play forces a launch into that slot; stop kills only it.
 * Re-clicking the currently-selected voice returns to Global.
 *
 * Usage from MainComponent's GUI-thread timer:
 *   auto desired = mode_controller_.tick(read_juce_bool("global"));
 *   set_bool_juce("global", desired.global_on);
 */
class ModeController
{
public:
    enum class Mode { Voice, Global };

    struct ButtonState { bool global_on; };

    using SnapshotFn = std::function<VoiceLiveParams()>;
    using ApplyFn    = std::function<void(const VoiceLiveParams&)>;

    ModeController(GuiInputData& gui_input,
                   const GuiOutputData& gui_output,
                   SnapshotFn snapshot_juce_sliders,
                   ApplyFn    apply_to_juce_sliders);

    /** Reconcile against the latest JUCE button state and return the
     *  buttons' desired state for the caller to write back. Also publishes
     *  global_mode to the audio thread. */
    ButtonState tick(bool global_juce);

    /** Called from MainComponent when the user clicks voice button N.
     *  Re-clicking the currently selected voice returns to Global. */
    void on_voice_button_clicked(size_t voice_index);

    Mode mode() const;
    int  selected_voice() const { return selected_voice_; }
    bool global_on()      const { return global_on_; }

private:
    void enter_voice_mode(size_t voice_index);
    void enter_global_mode();

    /** Pull a voice's published snapshot atomics back into a VoiceLiveParams,
     *  using the voice_param_table positions. */
    VoiceLiveParams read_voice_snapshot(size_t voice_index) const;

    GuiInputData&         gui_input_;
    const GuiOutputData&  gui_output_;
    SnapshotFn            snapshot_juce_;
    ApplyFn               apply_juce_;

    int  selected_voice_{-1};
    bool global_on_{true};   // Global is the default state
    VoiceLiveParams global_params_cache_{};
    bool global_params_cached_{false};
};

#endif
