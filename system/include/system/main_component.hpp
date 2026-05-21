#ifndef GUI_MAIN_COMPONENT_H
#define GUI_MAIN_COMPONENT_H

#include "instrument/parameter_data.hpp"
#include "system/panels.hpp"

#include "igui/igui.hpp"

#include "JuceHeader.h"

#include <cstddef>
#include <functional>
#include <memory>

class EngineAudioProcessor;
class EngineAudioProcessorEditor;

class MainComponent:
    public juce::Component,
    public juce::AsyncUpdater,
    public juce::Timer
{
public:
    MainComponent(EngineAudioProcessor& processor, EngineAudioProcessorEditor& editor);

    ~MainComponent();

    void paint (juce::Graphics&) override;
    void resized() override;

    void parentHierarchyChanged() override;

    void handleAsyncUpdate() override;

    void timerCallback() override;

    void check_file_chooser_request();

    /// @brief Called from a VoiceButtonContainer when its button is clicked.
    /// Implements radio-style toggle: clicking the currently-selected voice
    /// deselects; clicking another voice swaps the selection. On selection
    /// transitions, the JUCE parameter values are reloaded so the sliders
    /// reflect the new context (selected voice's live params, or the
    /// previously-cached global params).
    void on_voice_button_clicked(size_t voice_index);

    void open_audio_midi_settings_window();

    void show_settings_panel();
    void hide_settings_panel();
    bool toggle_settings_panel();

    void set_window_size_scale(float scale);

    template<class T>
    T scale_to_fit(const T& thing) const
    {
        return thing * this->window_scale;
    }

    struct WindowSize
    {
        int width;
        int height;
    };

    WindowSize get_current_window_full_size() const;

    struct WindowSizeOptions
    {
        WindowSize standard;
        WindowSize standard_with_settings;
    };

    const WindowSizeOptions window_full_size;

private:
    EngineAudioProcessor& processor;
    EngineAudioProcessorEditor& editor;

    std::unique_ptr<igui::FontLifetimeManager> font_lifetime_manager;

    MainPanel main_panel;
    SettingsPanel settings_panel;

    std::reference_wrapper<const WindowSize> current_window_full_size;
    float& window_scale;
    void refresh_window_size();

    class BoundsManager:
        public juce::ComponentBoundsConstrainer
    {
        public:
            BoundsManager(const MainComponent& main_comp);

            void update();

        private:
            void update_aspect_ratio();
            void update_size_limits();

            juce::Rectangle<int> get_display_area() const;

            const MainComponent& main_component;
    };
    BoundsManager bounds_manager;
    friend class BoundsManager;

    void set_using_native_title_bar();

    std::shared_ptr<juce::FileChooser> file_chooser;

    // Voice-select state (GUI-thread). -1 means "no selection" — combined with
    // global_on_ this gives the three modes: Auto (-1, false) / Voice N (>=0, false)
    // / Global (-1, true). The GUI enforces the radio invariant; the audio
    // thread reads the resulting selected_voice + global_mode atomics.
    int  selected_voice_{-1};
    bool global_on_{false};

    // Snapshot of the live-editable JUCE params taken when selection leaves
    // the no-voice state (selected_voice_ moves from -1 → N, or entering
    // Global). Restored back into the JUCE params when we return to the
    // no-voice state, so the user's "next launch" intent isn't lost.
    VoiceLiveParams global_params_cache_{};
    bool global_params_cached_{false};

    void refresh_voice_button_visuals();
    void apply_params_to_juce(const VoiceLiveParams& p);
    VoiceLiveParams snapshot_juce_params() const;
    VoiceLiveParams read_voice_snapshot(size_t voice_index) const;

    // Mode transitions (radio enforcement). Each one updates selected_voice_,
    // global_on_, the GuiInputData atomics, the auto/global JUCE button
    // params, and the voice-button LED visuals.
    void enter_auto_mode();
    void enter_voice_mode(size_t voice_index);
    void enter_global_mode();

    // JUCE bool-param helpers (shared by apply_params_to_juce and the mode
    // transitions). `set_bool_juce` writes through to JUCE + notifies the
    // host; `read_juce_bool` returns the current parameter value.
    void set_bool_juce(const juce::String& id, bool value);
    bool read_juce_bool(const juce::String& id) const;
};

/// @brief Gets the main component by searching up the component tree.
/// @param start_component Starting point for the search.
/// @return A pointer to the main component, or nullptr if not found.
/// @note This is super janky, put in as a bodge for alpha revision.
/// NOT FOR PRODUCTION CODE.
static MainComponent* get_main_component(const juce::Component& start_component)
{
    const juce::Component* comp = &start_component;
    while (true)
    {
        // Managed to reach the top-level component without finding the main
        // component; search failed
        if (comp == nullptr)
            return nullptr;

        const auto parent = comp->getParentComponent();
        const auto main_comp = dynamic_cast<MainComponent*>(parent);
        if (main_comp != nullptr)
            return main_comp;

        // Not yet found, search a level up
        comp = parent;
    }
}

#endif
