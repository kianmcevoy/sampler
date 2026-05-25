#ifndef GUI_MAIN_COMPONENT_H
#define GUI_MAIN_COMPONENT_H

#include "instrument/parameter_data.hpp"
#include "interface/mode_controller.hpp"
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

    // Three-mode radio (Auto / Voice N / Global) lives in interface/.
    // MainComponent provides it with JUCE-thread snap/apply callbacks and
    // pumps it from timerCallback / on_voice_button_clicked.
    ModeController mode_controller_;

    void refresh_voice_button_visuals();

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
