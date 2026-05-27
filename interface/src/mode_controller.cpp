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
    // Default Global: publish so the audio thread starts coherent.
    gui_input_.selected_voice.store(-1);
    gui_input_.global_mode.store(true);
}

ModeController::Mode ModeController::mode() const
{
    return (selected_voice_ >= 0) ? Mode::Voice : Mode::Global;
}

ModeController::ButtonState ModeController::tick(bool global_juce)
{
    // Detect a Global-button click. The caller re-asserts the returned
    // state so accidental flips (clicking Global while already in Global)
    // snap back without effect.
    if (global_juce != global_on_)
    {
        if (global_juce) enter_global_mode();
        // else: user clicked Global to turn it off. With Auto removed there's
        // no "off" state; Global stays on. The caller's set_bool_juce will
        // re-light the button.
    }

    // Publish per-tick so a stale reader can't see a half-changed mode.
    gui_input_.global_mode.store(global_on_);

    return { global_on_ };
}

void ModeController::on_voice_button_clicked(size_t voice_index)
{
    const int requested = static_cast<int>(voice_index);
    // Re-clicking the currently-selected voice deselects → Global mode.
    if (selected_voice_ == requested) enter_global_mode();
    else                              enter_voice_mode(voice_index);
}

void ModeController::deselect_voice()
{
    if (selected_voice_ >= 0) enter_global_mode();
}

void ModeController::enter_voice_mode(size_t voice_index)
{
    const int new_selection = static_cast<int>(voice_index);
    const bool was_in_global = (selected_voice_ == -1);

    // Capture current sliders as global params on transition out of Global
    // so deselect can restore them.
    if (was_in_global)
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

void ModeController::enter_global_mode()
{
    const bool leaving_voice = (selected_voice_ >= 0);

    // Restore the slider state we captured on entry to Voice mode.
    if (leaving_voice && global_params_cached_)
    {
        apply_juce_(global_params_cache_);
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
