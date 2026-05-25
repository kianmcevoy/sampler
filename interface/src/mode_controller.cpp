#include "interface/mode_controller.hpp"

#include "interface/gui_data.hpp"
#include "interface/voice_param_table.hpp"

ModeController::ModeController(GuiInputData& gui_input,
                               const GuiOutputData& gui_output,
                               SnapshotFn snapshot_juce_sliders,
                               ApplyFn    apply_to_juce_sliders):
    gui_input_{gui_input},
    gui_output_{gui_output},
    snapshot_juce_{std::move(snapshot_juce_sliders)},
    apply_juce_{std::move(apply_to_juce_sliders)}
{
    // Default Auto: publish so the audio thread starts coherent.
    gui_input_.selected_voice.store(-1);
    gui_input_.global_mode.store(false);
}

ModeController::Mode ModeController::mode() const
{
    if (selected_voice_ >= 0) return Mode::Voice;
    if (global_on_)           return Mode::Global;
    return Mode::Auto;
}

ModeController::ButtonState ModeController::tick(bool auto_juce, bool global_juce)
{
    // The Auto and Global JUCE bools are user-clickable. Compare them
    // against our internal truth to detect a click and run the transition.
    // The caller re-asserts the returned ButtonState so any accidental
    // toggle (e.g. clicking Auto while already in Auto) snaps back.
    if (global_juce != global_on_)
    {
        if (global_juce) enter_global_mode();
        else             enter_auto_mode();
    }
    else
    {
        const bool currently_in_auto = (selected_voice_ == -1 && !global_on_);
        if (auto_juce && !currently_in_auto)
        {
            enter_auto_mode();
        }
        // Other auto-flip cases (clicked Auto while in Auto, toggling off)
        // are corrected by the caller re-asserting the returned state.
    }

    // Publish per-tick so a stale reader can't see a half-changed mode.
    gui_input_.global_mode.store(global_on_);

    const bool in_auto = (selected_voice_ == -1 && !global_on_);
    return { in_auto, global_on_ };
}

void ModeController::on_voice_button_clicked(size_t voice_index)
{
    const int requested = static_cast<int>(voice_index);
    // Re-clicking the currently-selected voice deselects → Auto mode.
    if (selected_voice_ == requested) enter_auto_mode();
    else                              enter_voice_mode(voice_index);
}

void ModeController::enter_voice_mode(size_t voice_index)
{
    const int new_selection = static_cast<int>(voice_index);
    const bool was_no_voice = (selected_voice_ == -1);

    // Capture current sliders as global params on transition away from
    // no-voice so deselect can restore them.
    if (was_no_voice)
    {
        global_params_cache_  = snapshot_juce_();
        global_params_cached_ = true;
    }

    selected_voice_ = new_selection;
    global_on_      = false;
    gui_input_.selected_voice.store(new_selection);

    // Snap sliders to the voice's live values if it's currently sounding.
    if (gui_output_.voice_active[static_cast<size_t>(new_selection)].load())
    {
        apply_juce_(read_voice_snapshot(static_cast<size_t>(new_selection)));
    }
}

void ModeController::enter_auto_mode()
{
    const bool leaving_voice = (selected_voice_ >= 0);

    if (leaving_voice && global_params_cached_)
    {
        apply_juce_(global_params_cache_);
    }

    selected_voice_ = -1;
    global_on_      = false;
    gui_input_.selected_voice.store(-1);
}

void ModeController::enter_global_mode()
{
    const bool was_voice = (selected_voice_ >= 0);

    if (was_voice && !global_params_cached_)
    {
        global_params_cache_  = snapshot_juce_();
        global_params_cached_ = true;
    }

    selected_voice_ = -1;
    global_on_      = true;
    gui_input_.selected_voice.store(-1);
}

VoiceLiveParams ModeController::read_voice_snapshot(size_t voice_index) const
{
    const auto& src = gui_output_.voice_params_snapshot[voice_index];
    VoiceLiveParams out;
    for (size_t fi = 0; fi < voice_param_floats.size(); ++fi)
        out.*(voice_param_floats[fi].field) = src.floats[fi].load();
    for (size_t bi = 0; bi < voice_param_bools.size(); ++bi)
        out.*(voice_param_bools[bi].field) = src.bools[bi].load();
    return out;
}
