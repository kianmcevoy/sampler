#ifndef SYSTEM_CONTROL_BUILDER_H
#define SYSTEM_CONTROL_BUILDER_H

#include "JuceHeader.h"

class GuiControlBuilder
{
    public:
        struct ControlGeometry
        {
            float x;
            float y;
            float w;
            float h;
        };

        GuiControlBuilder() = default;
        ~GuiControlBuilder() = default;

        /** Sets the overall panel canvas size (in the 800-wide design coordinate
         *  space). Must be called from `build_gui_control_scheme` before any
         *  `add_*` calls so the window opens at the right size.
         */
        void set_panel_size(float width, float height);

        /** Sets the top-left position and size of the waveform display in
         *  design pixels. The waveform, cursors and start/end markers scale
         *  with these bounds. If not called, the display defaults to a
         *  700x150 area centred horizontally at y = 45.
         */
        void set_display(float x, float y, float width, float height);

        /** All add_* methods take `panel` as their first argument: the name
         *  of the user-facing panel that should host this control (e.g.
         *  "main", "modulation"). MainComponent's tab strip displays a tab
         *  per declared panel name and shows the matching panel underneath. */
        void add_slider(const juce::String& panel, const juce::String& identifier, const juce::String& label,
                        float x, float y, float w = 120.f, float h = 120.f);

        /** Add a slider with an explicit displayed range and default. The
         *  underlying JUCE parameter remains normalised [0, 1]; min/max/default
         *  are interpreted as the displayed values shown in the GUI and
         *  returned by `input.controls.sliders.at(id)` in ParameterInterface.
         */
        void add_slider(const juce::String& panel, const juce::String& identifier, const juce::String& label,
                        float min, float max, float default_value,
                        float x, float y, float w = 120.f, float h = 120.f);

        void add_button(const juce::String& panel, const juce::String& identifier, const juce::String& label,
                        float x, float y, float w = 120.f, float h = 120.f);

        /** Add a latching toggle with an explicit default state. */
        void add_button(const juce::String& panel, const juce::String& identifier, const juce::String& label,
                        bool default_value,
                        float x, float y, float w = 120.f, float h = 120.f);

        /** Add a voice-select button. Visually a LED button like add_button,
         *  but its LED is driven by per-voice active-state (audio→GUI) and
         *  its click toggles radio-style voice selection (writes selected
         *  voice index into GuiInputData). `voice_index` must be in
         *  [0, max_voices). */
        void add_voice_button(const juce::String& panel, const juce::String& identifier, const juce::String& label,
                              size_t voice_index,
                              float x, float y, float w = 120.f, float h = 120.f);

        void add_trigger(const juce::String& panel, const juce::String& identifier, const juce::String& label,
                         float x, float y, float w = 120.f, float h = 120.f);

        void add_dropdown(const juce::String& panel, const juce::String& identifier, const juce::String& label,
                          const juce::StringArray& options,
                          float x, float y, float w = 120.f, float h = 120.f);

        juce::AudioProcessorValueTreeState::ParameterLayout&& transfer_parameter_layout();

        const std::vector<juce::String>& get_all_parameter_identifiers() const;

        const std::vector<juce::String>& get_slider_identifiers() const;

        /** Returns the displayed range per slider, in declaration order.
         *  Sliders added via the simple `add_slider(id, label)` overload
         *  default to (0, 1).
         */
        const std::vector<juce::NormalisableRange<float>>& get_slider_ranges() const;

        const std::vector<juce::String>& get_button_identifiers() const;

        /** Voice-select buttons (in declaration order). */
        const std::vector<juce::String>& get_voice_button_identifiers() const;
        /** Per-voice-button: which voice slot (0..max_voices-1) it controls. */
        const std::vector<size_t>& get_voice_button_indices() const;

        const std::vector<juce::String>& get_trigger_identifiers() const;

        const std::vector<juce::String>& get_dropdown_identifiers() const;

        /** Per-control geometry (top-left x/y plus width/height) parallel to
         *  `get_all_parameter_identifiers()` — i.e. in declaration order.
         */
        const std::vector<ControlGeometry>& get_control_geometries() const;

        /** Per-control panel name, parallel to get_all_parameter_identifiers().
         *  Panels filter their child controls by matching this name. */
        const juce::String& panel_for(size_t i) const { return this->control_panels[i]; }

        float get_panel_width() const;
        float get_panel_height() const;

        /** Returns the configured waveform display geometry (top-left x/y,
         *  width, height). If `set_display` was not called, the geometry is
         *  derived from the panel width so the display sits centred at the
         *  top of the panel.
         */
        ControlGeometry get_display_geometry() const;

    private:
        void check_id(const juce::String& identifier) const;

        juce::AudioProcessorValueTreeState::ParameterLayout parameters;

        std::vector<juce::String> parameter_ids;
        std::vector<juce::String> slider_ids;
        std::vector<juce::String> button_ids;
        std::vector<juce::String> trigger_ids;
        std::vector<juce::String> dropdown_ids;
        std::vector<juce::String> voice_button_ids;
        std::vector<size_t>       voice_button_indices;

        // Per-slider displayed range, parallel to slider_ids.
        std::vector<juce::NormalisableRange<float>> slider_ranges;

        // Per-control geometry, parallel to parameter_ids.
        std::vector<ControlGeometry> control_geometries;

        // Per-control panel name, parallel to parameter_ids. Controls filter
        // into their respective panels by matching this name.
        std::vector<juce::String> control_panels;

        float panel_width  = 800.f;
        float panel_height = 600.f;

        // Waveform display geometry (top-left x/y + size). Negative x means
        // "no explicit position set — centre horizontally on the panel".
        ControlGeometry display_geometry{-1.f, 45.f, 700.f, 150.f};
};

#endif
