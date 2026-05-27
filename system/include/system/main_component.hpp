#ifndef GUI_MAIN_COMPONENT_H
#define GUI_MAIN_COMPONENT_H

#include "instrument/constants.hpp"
#include "instrument/parameter_data.hpp"
#include "interface/mode_controller.hpp"
#include "system/panels.hpp"

#include "igui/igui.hpp"

#include "JuceHeader.h"

#include <array>
#include <cstddef>
#include <functional>
#include <map>
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
    ModulationPanel modulation_panel;
    SettingsPanel settings_panel;

    // Tab buttons that swap which project panel is visible. Owned directly
    // by MainComponent so they're always visible regardless of which panel
    // is currently displayed.
    igui::LedButton main_tab_button;
    igui::LedButton modulation_tab_button;

    juce::String active_panel_{"main"};
    void set_active_panel(const juce::String& name);

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

    // Three-mode radio (Auto / Voice N / Global) lives in interface/.
    // MainComponent provides it with JUCE-thread snap/apply callbacks and
    // pumps it from timerCallback / on_voice_button_clicked.
    ModeController mode_controller_;

    void refresh_voice_button_visuals();

    // Layer view: re-paints the voice buttons as layer selectors, drawing
    // brightness from the audio thread's per-layer summed envelopes and
    // colour from "current layer" (gold) vs "other-active layer" (grey).
    void refresh_layer_button_visuals();

    // Radio invariant for voice_view ↔ layer_view JUCE buttons. When the
    // user clicks one, MainComponent un-clicks the other and clears the
    // voice selection (Voice mode doesn't survive into Layer view).
    void enforce_view_radio();

    // Tracks the current layer (mirrors gui_input.selected_layer). Written
    // by on_voice_button_clicked while in layer view; read by the timer to
    // paint the gold-vs-grey background.
    int  current_layer_ { 0 };
    // Latches the view state from JUCE so transitions can be detected
    // between ticks (for clearing selected_voice when entering layer view).
    bool layer_view_    { false };

    // Bulk JUCE param read/write of the per-voice live-editable set
    // (driven by interface/voice_param_table.hpp).
    void apply_params_to_juce(const VoiceLiveParams& p);
    VoiceLiveParams snapshot_juce_params() const;

    // Single-param JUCE helpers used by the bulk methods above and the
    // mode-controller tick (for the auto/global toggle bools).
    void  set_bool_juce  (const juce::String& id, bool value);
    bool  read_juce_bool (const juce::String& id) const;
    void  set_float_juce (const juce::String& id, float displayed_value);
    float read_float_juce(const juce::String& id, float fallback) const;
    void  set_choice_juce(const juce::String& id, int index);

    // Per-layer parameter snapshots for layer-switch snap-to-saved-state.
    // sliders store displayed values; dropdowns store selected indices.
    // Excludes triggers (momentary), voice buttons, voice_view/layer_view,
    // and the global toggle — all of which are view/session state, not
    // per-layer.
    struct LayerParamSnapshot
    {
        std::map<juce::String, float> sliders;
        std::map<juce::String, bool>  buttons;
        std::map<juce::String, int>   dropdowns;
    };
    std::array<LayerParamSnapshot, max_layers> layer_param_snapshots_ {};

    void save_juce_into_layer (size_t layer_index);
    void restore_layer_into_juce(size_t layer_index);
    bool is_per_layer_param   (const juce::String& id) const;
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
