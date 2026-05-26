#ifndef GUI_PANELS_H
#define GUI_PANELS_H

#include "system/control_builder.hpp"

#include "igui/igui.hpp"

#include "JuceHeader.h"

#include <memory>
#include <vector>

class EngineAudioProcessor;
class MainComponent;

class PanelBase:
    public juce::Component,
    public juce::AsyncUpdater
{
    public:
        PanelBase(EngineAudioProcessor& processor);

        virtual ~PanelBase();

        void resized() override;

        void moved() override;

        virtual void bounds_changed() = 0;

        void handleAsyncUpdate() override;

        void draw_panel_background(juce::Graphics& g, const juce::Rectangle<float>& bounds) const;
        void draw_panel_background(juce::Graphics& g) const;

        void stroke_path(juce::Graphics& g, const juce::Path& path, juce::PathStrokeType stroke_type) const;

        void fill_path(juce::Graphics& g, const juce::Path& path) const;

        void draw_dashed_line(juce::Graphics& g, juce::Line<float> line, std::vector<float> pattern, float thickness) const;
        void draw_dashed_line(juce::Graphics& g, juce::Line<float> line, std::vector<float> pattern) const;
        void draw_dashed_line(juce::Graphics& g, juce::Line<float> line, float thickness) const;
        void draw_dashed_line(juce::Graphics& g, juce::Line<float> line) const;

        /// @brief Sets the given component's bounds using coordinates relative
        /// to the window's origin.
        void place_component(juce::Component& component, float width, float height, float centre_x, float centre_y) const;
        /// @brief A specialisation of @ref place_component that automatically
        /// sizes the text's bounds to one that will fit the entire string.
        void place_text(igui::TextElement& text, float text_height, float centre_x, float centre_y) const;
        void place_text_box(igui::TextElement& text, float text_height, float box_width, float box_height, float centre_x, float centre_y, juce::Justification justification = juce::Justification::centredTop) const;

        void set_component_subdimensions(igui::Slider& slider, float track_thickness, float label_height) const;
        void set_component_subdimensions(igui::Slider& slider, float label_height, float bar_height, float outline_thickness_active, float outline_thickness_inactive) const;

        void set_component_subdimensions(igui::Button& button, float text_height, float corner_radius) const;
        void set_component_subdimensions(igui::Button& button, float corner_radius) const;

        void set_component_subdimensions(igui::LedButton& button, float text_height, float corner_radius, float outline_thickness) const;
        void set_component_subdimensions(igui::LedButton& button, float text_height, float corner_radius) const;
        void set_component_subdimensions(igui::LedButton& button, float corner_radius) const;

        void set_component_subdimensions(igui::ComboBox& combo_box, float text_height) const;
        void set_component_subdimensions(igui::ComboBox& combo_box, float text_height, float outline_thickness, float corner_radius) const;
        void set_component_subdimensions(igui::ComboBox& combo_box, float text_height, float outline_thickness, float corner_radius, float text_spacer_width) const;

    protected:
        juce::AffineTransform get_main_transform() const;

        float get_main_scale_factor() const;

        MainComponent& get_main_component() const;

        EngineAudioProcessor& audio_processor;

    private:
        MainComponent* _get_main_component() const;
        MainComponent* _main_component;
        void update_main_transform();
        juce::AffineTransform _main_transform;
        void update_main_scale_factor();
        float _main_scale_factor;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelBase)
};

class ControlContainer:
    public juce::Component
{
    public:
        ControlContainer() = default;

        void paint(juce::Graphics&) override {}

        virtual void set_subdimentions(float line_thickness, float text_height) = 0;
};

class VoiceButtonContainer; // Defined in panels.cpp.

class MainPanel final:
    public PanelBase
{
    public:
        MainPanel(EngineAudioProcessor& processor);

        void paint(juce::Graphics& g) override;

        void bounds_changed() override;

        /// @brief Number of voice-select buttons in the current scheme.
        size_t num_voice_buttons() const;
        /// @brief Voice slot (0..max_voices-1) controlled by the i-th voice
        /// button. Parallel to add_voice_button declaration order.
        size_t voice_slot_for_button(size_t button_idx) const;
        /// @brief Drive the LED brightness of voice button `button_idx` from a
        /// 0..1 amplitude (e.g. envelope × base level) so it fades with sound.
        void set_voice_brightness(size_t button_idx, float brightness);
        /// @brief Toggle the "selected" visual cue on voice button
        /// `button_idx`. Independent of activity state.
        void set_voice_selected(size_t button_idx, bool selected);
        /// @brief Set the JUCE alpha on the slider whose param id matches
        /// `id` (1.0 = normal, 0.4 = greyed). No-op if not found.
        void set_slider_alpha(const juce::String& id, float alpha);
        /// @brief True if the user is currently dragging the slider whose
        /// param id matches `id`. Returns false if not found.
        bool is_slider_being_gestured(const juce::String& id) const;

        igui::LedButton settings_menu_button;

        std::vector<std::unique_ptr<ControlContainer>> controls;
        // Per-control geometry, parallel to `controls`. Populated alongside
        // controls during construction (only entries for this panel's name).
        std::vector<GuiControlBuilder::ControlGeometry> control_geometries;

        std::unique_ptr<juce::Component> waveform_display;

    private:
        // Non-owning pointers into `controls`. Parallel to the order in which
        // add_voice_button was called.
        std::vector<VoiceButtonContainer*> voice_button_containers;
};

/** Secondary project panel: hosts only the controls tagged "modulation".
 *  Same canvas size as MainPanel; only one of the two is visible at a time
 *  (selected via tab buttons in MainComponent). */
class ModulationPanel final:
    public PanelBase
{
    public:
        ModulationPanel(EngineAudioProcessor& processor);

        void paint(juce::Graphics& g) override;

        void bounds_changed() override;

        std::vector<std::unique_ptr<ControlContainer>> controls;
        std::vector<GuiControlBuilder::ControlGeometry> control_geometries;
};

class SettingsPanel final:
    public PanelBase
{
    public:
        SettingsPanel(EngineAudioProcessor& processor);

        void paint(juce::Graphics& g) override;

        void bounds_changed() override;

        igui::Button open_audio_midi_settings_button;

        igui::InstruoTextElement osc_input_port_label;
        igui::TextField osc_input_port_number_editor;

        igui::InstruoTextElement osc_output_port_label;
        igui::TextField osc_output_port_number_editor;

        igui::InstruoTextElement min_max_section_heading;
        igui::InstruoTextElement param_column_name;
        igui::InstruoTextElement min_column_name;
        igui::InstruoTextElement max_column_name;

        std::vector<std::unique_ptr<igui::InstruoTextElement>> labels;
        std::vector<std::unique_ptr<igui::TextField>> min_fields;
        std::vector<std::unique_ptr<igui::TextField>> max_fields;

    private:
        void indicate_osc_port_number_validity(igui::TextField& editor, bool is_valid);
};

#endif
