#include "system/panels.hpp"

#include "system/asset_manager.hpp"
#include "system/engine.hpp"
#include "system/main_component.hpp"
#include "system/platform.hpp"

// Waveform display geometry is now provided by GuiControlBuilder via
// `set_display(x, y, w, h)` in the user's control scheme. See controls.cpp.

PanelBase::PanelBase(EngineAudioProcessor& ap):
audio_processor{ap},
_main_component{nullptr}
{}

PanelBase::~PanelBase()
{
    this->cancelPendingUpdate();
}

void PanelBase::resized()
{
    this->update_main_transform();
    this->update_main_scale_factor();

    this->bounds_changed();
}

void PanelBase::moved()
{
    this->resized();
}

void PanelBase::handleAsyncUpdate()
{
    this->repaint();
}

void PanelBase::draw_panel_background(juce::Graphics& g, const juce::Rectangle<float>& bounds) const
{
    g.setColour(igui::colours::dark_grey);
    g.fillRoundedRectangle(bounds, 9.862 * this->get_main_scale_factor());
}

void PanelBase::draw_panel_background(juce::Graphics& g) const
{
    this->draw_panel_background(g, this->getLocalBounds().toFloat());
}

void PanelBase::stroke_path(juce::Graphics& g, const juce::Path& path, juce::PathStrokeType stroke_type) const
{
    stroke_type.setStrokeThickness(stroke_type.getStrokeThickness() * this->get_main_scale_factor());
    g.strokePath(path, stroke_type, this->get_main_transform());
}

void PanelBase::fill_path(juce::Graphics& g, const juce::Path& path) const
{
    g.fillPath(path, this->get_main_transform());
}

void PanelBase::draw_dashed_line(juce::Graphics& g, juce::Line<float> line, std::vector<float> pattern, float thickness) const
{
    const auto scaler = this->get_main_scale_factor();
    for (auto& dash : pattern)
        dash *= scaler;
    line.applyTransform(this->get_main_transform());
    g.drawDashedLine(line, pattern.data(), pattern.size(), thickness * scaler);
}

void PanelBase::draw_dashed_line(juce::Graphics& g, juce::Line<float> line, std::vector<float> pattern) const
{
    this->draw_dashed_line(g, line, pattern, 1.f);
}

void PanelBase::draw_dashed_line(juce::Graphics& g, juce::Line<float> line, float thickness) const
{
    this->draw_dashed_line(g, line, {3.f, 4.f}, thickness);
}

void PanelBase::draw_dashed_line(juce::Graphics& g, juce::Line<float> line) const
{
    this->draw_dashed_line(g, line, 1.f);
}

void PanelBase::place_component(juce::Component& component, float width, float height, float centre_x, float centre_y) const
{
    juce::Rectangle<float> bounds(width, height);
    bounds.setCentre(centre_x, centre_y);

    // Translate the bounds from global coordinates to local ones
    bounds = bounds.transformedBy(this->get_main_transform());

    component.setBounds(bounds.toNearestInt());
}

void PanelBase::place_text(igui::TextElement& text, float text_height, float centre_x, float centre_y) const
{
    const auto scaler = this->get_main_scale_factor();
    const auto string_width = text.get_font().withHeight(text_height * scaler).getStringWidthFloat(text.get_text());
    this->place_component(text, string_width * 1.5, text_height, centre_x, centre_y);
}

void PanelBase::place_text_box(igui::TextElement& text, float text_height, float box_width, float box_height, float centre_x, float centre_y, juce::Justification justification) const
{
    const auto scaler = this->get_main_scale_factor();
    this->place_component(text, box_width, box_height, centre_x, centre_y);
    text.set_font_height(text_height * scaler * 0.85f);
    text.set_justification(justification);
}

void PanelBase::set_component_subdimensions(igui::Slider& slider, float track_thickness, float label_height) const
{
    const auto scaler = this->get_main_scale_factor();
    const auto view = dynamic_cast<igui::InstruoKnobVectorElement*>(&slider.view());
    jassert(view != nullptr);

    view->set_track_thickness(track_thickness * scaler);
    view->set_label_height(label_height * scaler);
}

void PanelBase::set_component_subdimensions(igui::Slider& slider, float label_height, float bar_height, float outline_thickness_active, float outline_thickness_inactive) const
{
    const auto scaler = this->get_main_scale_factor();
    const auto view = dynamic_cast<igui::InstruoSquareKnobVectorElement*>(&slider.view());
    jassert(view != nullptr);

    view->set_label_height(label_height * scaler);
    view->set_bar_height(bar_height * scaler);
    view->set_outline_thickness(outline_thickness_active * scaler, outline_thickness_inactive * scaler);
}

void PanelBase::set_component_subdimensions(igui::Button& button, float text_height, float corner_radius) const
{
    const auto scaler = this->get_main_scale_factor();
    const auto view = dynamic_cast<igui::InstruoTextButtonElement*>(&button.view());
    jassert(view != nullptr);

    view->set_text_height(text_height * scaler);
    view->set_corner_radius(corner_radius * scaler);
}

void PanelBase::set_component_subdimensions(igui::Button& button, float corner_radius) const
{
    const auto scaler = this->get_main_scale_factor();
    const auto view = dynamic_cast<igui::InstruoTextButtonElement*>(&button.view());
    jassert(view != nullptr);

    view->set_corner_radius(corner_radius * scaler);
}

void PanelBase::set_component_subdimensions(igui::LedButton& button, float text_height, float corner_radius, float outline_thickness) const
{
    const auto scaler = this->get_main_scale_factor();
    const auto view = dynamic_cast<igui::InstruoLedTextButtonElement*>(&button.view());
    jassert(view != nullptr);

    view->set_text_height(text_height * scaler);
    view->set_corner_radius(corner_radius * scaler);
    view->set_outline_thickness(outline_thickness * scaler);
}

void PanelBase::set_component_subdimensions(igui::LedButton& button, float text_height, float corner_radius) const
{
    const auto scaler = this->get_main_scale_factor();
    const auto view = dynamic_cast<igui::InstruoLedTextButtonElement*>(&button.view());
    jassert(view != nullptr);

    view->set_text_height(text_height * scaler);
    view->set_corner_radius(corner_radius * scaler);
}

void PanelBase::set_component_subdimensions(igui::LedButton& button, float corner_radius) const
{
    const auto scaler = this->get_main_scale_factor();
    const auto view = dynamic_cast<igui::InstruoLedTextButtonElement*>(&button.view());
    jassert(view != nullptr);

    view->set_corner_radius(corner_radius * scaler);
}

void PanelBase::set_component_subdimensions(igui::ComboBox& combo_box, float text_height) const
{
    const auto scaler = this->get_main_scale_factor();
    const auto view = dynamic_cast<igui::InstruoComboBoxElement*>(&combo_box.view());
    jassert(view != nullptr);

    view->set_text_height(text_height * scaler);
}

void PanelBase::set_component_subdimensions(igui::ComboBox& combo_box, float text_height, float outline_thickness, float corner_radius) const
{
    const auto scaler = this->get_main_scale_factor();
    const auto view = dynamic_cast<igui::InstruoComboBoxElement*>(&combo_box.view());
    jassert(view != nullptr);

    view->set_text_height(text_height * scaler);
    view->set_outline_thickness(outline_thickness * scaler);
    view->set_outline_corner_radius(corner_radius * scaler);
}

void PanelBase::set_component_subdimensions(igui::ComboBox& combo_box, float text_height, float outline_thickness, float corner_radius, float text_spacer_width) const
{
    const auto scaler = this->get_main_scale_factor();
    const auto view = dynamic_cast<igui::InstruoDropdownMenuElement*>(&combo_box.view());
    jassert(view != nullptr);

    view->set_text_height(text_height * scaler);
    view->set_outline_thickness(outline_thickness * scaler);
    view->set_outline_corner_radius(corner_radius * scaler);
    view->set_text_spacer_width(text_spacer_width * scaler);
}

juce::AffineTransform PanelBase::get_main_transform() const
{
    return this->_main_transform;
}

float PanelBase::get_main_scale_factor() const
{
    return this->_main_scale_factor;
}

MainComponent& PanelBase::get_main_component() const
{
    const auto main_comp = this->_get_main_component();
    jassert(main_comp != nullptr);
    return *main_comp;
}

MainComponent* PanelBase::_get_main_component() const
{
    // This is safe as all panels are contained within the main component; not
    // possible for the main component to be deleted without also deleting the
    // panel and therefore its pointer to the main component
    if (this->_main_component != nullptr)
        return this->_main_component;
    const auto main_comp = ::get_main_component(*this);
    const_cast<PanelBase*>(this)->_main_component = main_comp;
    return this->_main_component;
}

void PanelBase::update_main_transform()
{
    const auto& main_component = this->get_main_component();

    const auto bounds = this->getLocalBounds().toFloat();

    // Origin of the panel relative to itself i.e. (0, 0)
    const auto origin = bounds.getTopLeft();
    // Origin of the panel relative to main
    const auto origin_in_main = main_component.getLocalPoint(this, origin);

    // Translate the bounds from global coordinates to local ones
    auto transform = juce::AffineTransform{}
        .scaled(main_component.scale_to_fit(1.f))
        .translated(origin - origin_in_main);

    this->_main_transform = transform;
}

void PanelBase::update_main_scale_factor()
{
    const auto& main_component = this->get_main_component();

    this->_main_scale_factor = main_component.scale_to_fit(1.f);
}


class SliderContainer:
    public ControlContainer
{
    public:
        SliderContainer(juce::RangedAudioParameter* parameter, igui::SliderElement* view):
        slider(parameter, view)
        {
            this->addAndMakeVisible(this->slider);
        }

        void resized() override
        {
            this->slider.setBounds(this->getLocalBounds().getProportion(
                juce::Rectangle<float>(0.1, 0.1, 0.8, 0.8)
            ));
        }

        void set_subdimentions(float line_thickness, float text_height)
        {
            auto* view = dynamic_cast<igui::InstruoKnobVectorElement*>(&this->slider.view());
            if (view != nullptr)
            {
                view->set_track_thickness(line_thickness);
                view->set_label_height(text_height);
            }
        }

        igui::Slider slider;
};

class ButtonContainer:
    public ControlContainer
{
    public:
        ButtonContainer(juce::RangedAudioParameter* parameter, igui::LedButtonElement* view):
        button(parameter, view)
        {
            this->addAndMakeVisible(this->button);
            this->button.set_to_standard_led_button_behaviour();
        }

        void resized() override
        {
            this->button.setBounds(this->getLocalBounds().getProportion(
                juce::Rectangle<float>(0.1, 0.35, 0.8, 0.3)
            ));
        }

        void set_subdimentions(float line_thickness, float text_height)
        {
            auto* view = dynamic_cast<igui::InstruoLedTextButtonElement*>(&this->button.view());
            if (view != nullptr)
            {
                view->set_outline_thickness(line_thickness);
                view->set_text_height(text_height);
            }
        }

        igui::LedButton button;
};

class VoiceButtonContainer:
    public ControlContainer
{
    public:
        VoiceButtonContainer(juce::RangedAudioParameter* parameter, igui::LedButtonElement* view,
                             size_t voice_index, std::function<void(size_t)> on_click):
        button(parameter, view),
        voice_index_{voice_index},
        on_click_{std::move(on_click)}
        {
            this->addAndMakeVisible(this->button);
            this->button.set_on_click_function([this]()
            {
                if (this->on_click_) this->on_click_(this->voice_index_);
            });
        }

        void resized() override
        {
            this->button.setBounds(this->getLocalBounds().getProportion(
                juce::Rectangle<float>(0.1, 0.35, 0.8, 0.3)
            ));
        }

        void set_subdimentions(float line_thickness, float text_height) override
        {
            auto* view = dynamic_cast<igui::InstruoLedTextButtonElement*>(&this->button.view());
            if (view != nullptr)
            {
                view->set_outline_thickness(line_thickness);
                view->set_text_height(text_height);
            }
        }

        // Drive LED brightness from the audio thread's per-voice amplitude
        // (envelope × base level). 0 = silent (LED off), 1 = peak.
        void set_brightness(float brightness)
        {
            this->button.view_led().brightness().set(juce::jlimit(0.f, 1.f, brightness));
        }

        // Selection visual: light the LED a little even when the voice isn't
        // active, and tint the button background so it stands out from the
        // other slots.
        void set_selected(bool selected)
        {
            if (selected == selected_) return;
            selected_ = selected;
            auto* view = dynamic_cast<igui::InstruoLedTextButtonElement*>(&this->button.view());
            if (view != nullptr)
            {
                view->set_background_colour(selected ? igui::colours::gold.withAlpha(0.25f)
                                                     : igui::colours::dark_grey);
                view->repaint();
            }
        }

        size_t voice_index() const { return voice_index_; }

        igui::LedButton button;

    private:
        size_t voice_index_;
        std::function<void(size_t)> on_click_;
        bool selected_{false};
};

class TriggerContainer:
    public ControlContainer
{
    public:
        TriggerContainer(juce::RangedAudioParameter* parameter, igui::ButtonElement* view):
        button(parameter, view)
        {
            this->addAndMakeVisible(this->button);
            this->button.set_on_click_function([this]()
            {
                const auto param = this->button.parameter();
                const auto bool_param = dynamic_cast<juce::AudioParameterBool*>(param);
                jassert(bool_param != nullptr);
                param->setValueNotifyingHost(param->convertTo0to1(true));
            });

            auto* const view_casted = dynamic_cast<igui::InstruoTextButtonElement*>(view);
            if (view_casted != nullptr)
            {
                view_casted->enable_gradient_fill(true);
                view_casted->set_background_colour(igui::colours::grey);
            }
        }

        void resized() override
        {
            this->button.setBounds(this->getLocalBounds().getProportion(
                juce::Rectangle<float>(0.1, 0.35, 0.8, 0.3)
            ));
        }

        void set_subdimentions(float line_thickness, float text_height)
        {
            auto* view = dynamic_cast<igui::InstruoTextButtonElement*>(&this->button.view());
            if (view != nullptr)
            {
                view->set_text_height(text_height);
            }
        }

        igui::Button button;
};

class DropdownContainer:
    public ControlContainer
{
    public:
        DropdownContainer(juce::RangedAudioParameter* parameter, igui::ComboBoxElement* view, const juce::String& label_text):
        combobox(parameter, view),
        label(label_text)
        {
            this->addAndMakeVisible(this->label);
            this->addAndMakeVisible(this->combobox);
            this->combobox.add_all_parameter_value_strings();
        }

        void resized() override
        {
            this->label.setBounds(this->getLocalBounds().getProportion(
                juce::Rectangle<float>(0.1, 0.2, 0.8, 0.2)
            ));
            this->combobox.setBounds(this->getLocalBounds().getProportion(
                juce::Rectangle<float>(0.1, 0.5, 0.8, 0.3)
            ));
        }

        void set_subdimentions(float line_thickness, float text_height)
        {
            auto* view = dynamic_cast<igui::InstruoComboBoxElement*>(&this->combobox.view());
            if (view != nullptr)
            {
                view->set_outline_thickness(line_thickness);
                view->set_text_height(text_height);
            }
            this->label.set_font_height(text_height);
        }

        igui::ComboBox combobox;
        igui::InstruoTextElement label;
};

class WaveformDisplay:
    public juce::Component,
    public juce::Timer
{
    public:
        WaveformDisplay(EngineAudioProcessor& processor):
        audio_processor{processor}
        {
            this->startTimer(100); // Check for updates every 100ms
        }

        void paint(juce::Graphics& g) override
        {
            // Draw background
            g.fillAll(juce::Colours::black);

            // Draw border
            g.setColour(igui::colours::light_grey);
            g.drawRect(this->getLocalBounds(), 2);

            // Get waveform data from GUI output (thread-safe copy)
            const auto& gui_output = this->audio_processor.get_gui_output_data();

            // Check if we have any waveform data
            if (!gui_output.waveform_ready.load() || gui_output.waveform_left.empty())
            {
                // Draw "No sample loaded" text
                g.setColour(igui::colours::grey);
                g.drawText("No sample loaded", this->getLocalBounds(), juce::Justification::centred);
                return;
            }

            const auto& waveform = gui_output.waveform_left;

            // Draw waveform
            const auto bounds = this->getLocalBounds().reduced(4).toFloat();
            const int num_samples = static_cast<int>(waveform.size());
            const float width = bounds.getWidth();
            const float height = bounds.getHeight();
            const float centre_y = bounds.getCentreY();

            // Get selection range
            const int start_sample = gui_output.display_marker_start.load();
            const int end_sample = gui_output.display_marker_end.load();

            // Calculate how many samples per pixel
            const int samples_per_pixel = std::max(1, num_samples / static_cast<int>(width));

            // Draw highlighted selection region
            if (start_sample >= 0 && end_sample > start_sample && end_sample <= num_samples)
            {
                const float start_x = bounds.getX() + (static_cast<float>(start_sample) / num_samples) * width;
                const float end_x = bounds.getX() + (static_cast<float>(end_sample) / num_samples) * width;
                const float selection_width = end_x - start_x;

                g.setColour(igui::colours::gold.withAlpha(0.15f));
                g.fillRect(start_x, bounds.getY(), selection_width, height);
            }

            juce::Path waveform_path;
            bool first_point = true;

            // Draw channel 0 (left)
            g.setColour(igui::colours::gold);
            waveform_path.clear();

            for (int x = 0; x < static_cast<int>(width); ++x)
            {
                const int sample_start = x * samples_per_pixel;
                const int sample_end = std::min(sample_start + samples_per_pixel, num_samples);

                if (sample_start >= num_samples) break;

                // Find min and max in this pixel range
                float min_val = 0.0f;
                float max_val = 0.0f;

                for (int s = sample_start; s < sample_end; ++s)
                {
                    const float val = waveform[s];
                    min_val = std::min(min_val, val);
                    max_val = std::max(max_val, val);
                }

                // Convert to screen coordinates
                const float x_pos = bounds.getX() + x;
                const float y_min = centre_y - (max_val * height * 0.4f);
                const float y_max = centre_y - (min_val * height * 0.4f);

                if (first_point)
                {
                    waveform_path.startNewSubPath(x_pos, y_min);
                    first_point = false;
                }

                waveform_path.lineTo(x_pos, y_min);
                waveform_path.lineTo(x_pos, y_max);
            }

            g.strokePath(waveform_path, juce::PathStrokeType(1.0f));

            // Draw start and end markers (gold vertical lines)
            if (start_sample >= 0 && end_sample > start_sample && end_sample <= num_samples)
            {
                const float start_x = bounds.getX() + (static_cast<float>(start_sample) / num_samples) * width;
                const float end_x = bounds.getX() + (static_cast<float>(end_sample) / num_samples) * width;

                g.setColour(igui::colours::gold.withAlpha(0.7f));
                g.drawLine(start_x, bounds.getY(), start_x, bounds.getBottom(), 2.0f);
                g.drawLine(end_x, bounds.getY(), end_x, bounds.getBottom(), 2.0f);
            }

            // Draw voice cursors (gold lines with brightness based on envelope level)
            for (size_t i = 0; i < max_voices; ++i)
            {
                const bool is_active = gui_output.voice_active[i].load();
                if (is_active)
                {
                    const float voice_pos = gui_output.voice_position[i].load();
                    const float voice_vol = gui_output.voice_volume[i].load();

                    // Only draw if position is within valid range
                    if (voice_pos >= 0.0f && voice_pos < static_cast<float>(num_samples))
                    {
                        const float voice_x = bounds.getX() + (voice_pos / num_samples) * width;
                        // Use envelope level to modulate brightness (alpha)
                        const float alpha = juce::jlimit(0.0f, 1.0f, voice_vol);
                        g.setColour(igui::colours::gold.withAlpha(alpha));
                        g.drawLine(voice_x, bounds.getY(), voice_x, bounds.getBottom(), 1.5f);
                    }
                }

            }

            // Draw centre line
            g.setColour(igui::colours::grey.withAlpha(0.5f));
            g.drawLine(bounds.getX(), centre_y, bounds.getRight(), centre_y, 1.0f);
        }

        void timerCallback() override
        {
            const auto& gui_output = this->audio_processor.get_gui_output_data();

            if (gui_output.file_loaded.load())
            {
                this->repaint();
                return;
            }

            const int current_start = gui_output.display_marker_start.load();
            const int current_end = gui_output.display_marker_end.load();

            bool any_voice_active = false;
            for (size_t i = 0; i < max_voices; ++i)
            {
                if (gui_output.voice_active[i].load())
                {
                    any_voice_active = true;
                    break;
                }
            }

            // Repaint while any voice is alive (so cursors animate) AND on the
            // active→inactive edge (so the final cursor disappears), plus on
            // start/end changes.
            const bool voices_changed = any_voice_active || this->last_any_voice_active;
            if (current_start != this->last_start || current_end != this->last_end || voices_changed)
            {
                this->last_start = current_start;
                this->last_end = current_end;
                this->last_any_voice_active = any_voice_active;
                this->repaint();
            }
        }

    private:
        EngineAudioProcessor& audio_processor;
        int last_start = -1;
        int last_end = -1;
        bool last_any_voice_active = false;
};


namespace
{
    // Walk all_params and instantiate the matching ControlContainer for any
    // entry whose builder-side panel name matches `panel_name`. The per-type
    // indices advance unconditionally so the next match works even when we
    // skip a control. Voice-button click handlers go through the supplied
    // callback (typically MainComponent::on_voice_button_clicked) wherever
    // the voice buttons land.
    void populate_panel_controls(
        EngineAudioProcessor& ap,
        const juce::String& panel_name,
        std::vector<std::unique_ptr<ControlContainer>>& out_controls,
        std::vector<GuiControlBuilder::ControlGeometry>& out_geometries,
        std::vector<VoiceButtonContainer*>* out_voice_buttons,
        const std::function<void(size_t)>& voice_button_on_click)
    {
        const auto& builder    = ap.get_gui_control_builder();
        const auto& geometries = builder.get_control_geometries();

        size_t float_params_index        = 0;
        size_t bool_params_index         = 0;
        size_t trigger_params_index      = 0;
        size_t choice_params_index       = 0;
        size_t voice_button_params_index = 0;

        for (size_t i = 0; i < ap.all_params.size(); ++i)
        {
            auto* param = ap.all_params[i];
            const bool keep = (builder.panel_for(i) == panel_name);

            if ((voice_button_params_index < ap.voice_button_params.size())
            && (param == ap.voice_button_params[voice_button_params_index]))
            {
                if (keep)
                {
                    const size_t voice_idx = ap.voice_button_indices[voice_button_params_index];
                    out_controls.emplace_back(std::make_unique<VoiceButtonContainer>(
                        param,
                        new igui::InstruoLedTextButtonElement(
                            param->getName(32),
                            igui::InstruoLedTextButtonElement::IndicationStyle::Outline
                        ),
                        voice_idx,
                        voice_button_on_click
                    ));
                    out_geometries.push_back(geometries[i]);
                    if (out_voice_buttons != nullptr)
                    {
                        out_voice_buttons->push_back(
                            static_cast<VoiceButtonContainer*>(out_controls.back().get())
                        );
                    }
                }
                voice_button_params_index++;
            }
            else if ((float_params_index < ap.float_params.size())
            && (param == ap.float_params[float_params_index]))
            {
                if (keep)
                {
                    out_controls.emplace_back(std::make_unique<SliderContainer>(
                        param,
                        new igui::InstruoKnobVectorElement(
                            param->getName(32),
                            [&ap, param, float_params_index] -> juce::String
                            {
                                const auto& range = ap.float_param_ranges[float_params_index];
                                const auto value = range.convertFrom0to1(param->getValue());
                                const auto id = param->getParameterID();

                                if (id == "pan")
                                {
                                    // Bipolar around 0.5 — <0.5 left / >0.5 right.
                                    return juce::String((value - 0.5f) * 200.f, 0) + "%";
                                }
                                if (id == "start" || id == "length" || id == "level"
                                    || id == "random_speed" || id == "random_start" || id == "random_length"
                                    || id == "random_level" || id == "random_pan")
                                {
                                    return juce::String(value * 100.f, 0) + "%";
                                }
                                // speed (-4..4) and anything else: show the raw value.
                                return juce::String(value, 2);
                            }
                        )
                    ));
                    out_geometries.push_back(geometries[i]);

                    // Bipolar sliders (default sits at 0.5 of the normalised
                    // range) get a centre reference so the active arc fills
                    // outward from the midpoint.
                    const float norm_default = param->getDefaultValue();
                    if (norm_default > 0.49f && norm_default < 0.51f)
                    {
                        auto* container = dynamic_cast<SliderContainer*>(out_controls.back().get());
                        if (container != nullptr)
                        {
                            auto* knob_view = dynamic_cast<igui::InstruoKnobVectorElement*>(&container->slider.view());
                            if (knob_view != nullptr) knob_view->set_reference_point(0.5f);
                        }
                    }
                }
                float_params_index++;
            }
            else if ((bool_params_index < ap.bool_params.size())
            && (param == ap.bool_params[bool_params_index]))
            {
                if (keep)
                {
                    out_controls.emplace_back(std::make_unique<ButtonContainer>(
                        param,
                        new igui::InstruoLedTextButtonElement(
                            param->getName(32),
                            igui::InstruoLedTextButtonElement::IndicationStyle::Outline
                        )
                    ));
                    out_geometries.push_back(geometries[i]);
                }
                bool_params_index++;
            }
            else if ((trigger_params_index < ap.trigger_params.size())
            && (param == ap.trigger_params[trigger_params_index]))
            {
                if (keep)
                {
                    out_controls.emplace_back(std::make_unique<TriggerContainer>(
                        param,
                        new igui::InstruoTextButtonElement(param->getName(32))
                    ));
                    out_geometries.push_back(geometries[i]);
                }
                trigger_params_index++;
            }
            else if ((choice_params_index < ap.choice_params.size())
            && (param == ap.choice_params[choice_params_index]))
            {
                if (keep)
                {
                    out_controls.emplace_back(std::make_unique<DropdownContainer>(
                        param,
                        new igui::InstruoComboBoxElement(),
                        param->getName(32)
                    ));
                    out_geometries.push_back(geometries[i]);
                }
                choice_params_index++;
            }
            else
            {
                jassertfalse;
            }
        }
    }
}

MainPanel::MainPanel(EngineAudioProcessor& ap):
PanelBase(ap),
settings_menu_button(new igui::InstruoLedButtonImageElement(
    AssetManager::get_real_resource_path("gui/assets/icons/settings_cog_white.png"),
    AssetManager::get_real_resource_path("gui/assets/icons/settings_cog_gold.png")
))
{
    this->addAndMakeVisible(this->settings_menu_button);
    this->settings_menu_button.set_on_click_function([this]()
    {
        this->get_main_component().toggle_settings_panel();
    });
    this->settings_menu_button.set_brightness_as_state_listener();

    populate_panel_controls(
        ap, "main",
        this->controls, this->control_geometries,
        &this->voice_button_containers,
        [this](size_t clicked_voice)
        {
            this->get_main_component().on_voice_button_clicked(clicked_voice);
        });

    for (auto& control : this->controls)
    {
        this->addAndMakeVisible(*control);
    }

    // Add waveform display
    this->waveform_display = std::make_unique<WaveformDisplay>(ap);
    this->addAndMakeVisible(*this->waveform_display);
}

void MainPanel::paint(juce::Graphics& g)
{
    this->draw_panel_background(g);
}

size_t MainPanel::num_voice_buttons() const
{
    return this->voice_button_containers.size();
}

size_t MainPanel::voice_slot_for_button(size_t button_idx) const
{
    return this->voice_button_containers[button_idx]->voice_index();
}

void MainPanel::set_voice_brightness(size_t button_idx, float brightness)
{
    this->voice_button_containers[button_idx]->set_brightness(brightness);
}

void MainPanel::set_voice_selected(size_t button_idx, bool selected)
{
    this->voice_button_containers[button_idx]->set_selected(selected);
}

void MainPanel::set_slider_alpha(const juce::String& id, float alpha)
{
    for (auto& control : this->controls)
    {
        auto* sc = dynamic_cast<SliderContainer*>(control.get());
        if (sc == nullptr) continue;
        const auto* param = sc->slider.parameter();
        if (param != nullptr && param->getParameterID() == id)
        {
            sc->setAlpha(alpha);
            return;
        }
    }
}

bool MainPanel::is_slider_being_gestured(const juce::String& id) const
{
    for (const auto& control : this->controls)
    {
        const auto* sc = dynamic_cast<const SliderContainer*>(control.get());
        if (sc == nullptr) continue;
        const auto* param = sc->slider.parameter();
        if (param != nullptr && param->getParameterID() == id)
            return sc->slider.parameter_being_gestured();
    }
    return false;
}

void MainPanel::bounds_changed()
{
    const auto panel_full_size = this->get_main_component().window_full_size.standard;

    this->place_component(this->settings_menu_button, 20.9009, 21.5605, panel_full_size.width - 20, 20);

    // Waveform placed at the position configured via `set_display` in the
    // user's control scheme; falls back to a sensible centred default.
    const auto display = this->audio_processor.get_gui_control_builder().get_display_geometry();
    this->place_component(*this->waveform_display, display.w, display.h,
                          display.x + display.w / 2.f, display.y + display.h / 2.f);

    jassert(this->control_geometries.size() == this->controls.size());
    for (size_t i = 0; i < this->controls.size(); ++i)
    {
        auto& control = *this->controls[i];
        const auto& g = this->control_geometries[i];
        // x/y are top-left in the design canvas; place_component expects centre.
        this->place_component(control, g.w, g.h, g.x + g.w / 2.f, g.y + g.h / 2.f);

        if (dynamic_cast<SliderContainer*>(&control) != nullptr)
        {
            control.set_subdimentions(4.f, 12);
        }
        else
        {
            control.set_subdimentions(2.f, 12);
        }
    }
}

ModulationPanel::ModulationPanel(EngineAudioProcessor& ap):
PanelBase(ap)
{
    populate_panel_controls(
        ap, "modulation",
        this->controls, this->control_geometries,
        /*out_voice_buttons=*/nullptr,
        /*voice_button_on_click=*/{});

    for (auto& control : this->controls)
    {
        this->addAndMakeVisible(*control);
    }
}

void ModulationPanel::paint(juce::Graphics& g)
{
    this->draw_panel_background(g);
}

void ModulationPanel::bounds_changed()
{
    jassert(this->control_geometries.size() == this->controls.size());
    for (size_t i = 0; i < this->controls.size(); ++i)
    {
        auto& control = *this->controls[i];
        const auto& g = this->control_geometries[i];
        this->place_component(control, g.w, g.h, g.x + g.w / 2.f, g.y + g.h / 2.f);

        if (dynamic_cast<SliderContainer*>(&control) != nullptr)
        {
            control.set_subdimentions(4.f, 12);
        }
        else
        {
            control.set_subdimentions(2.f, 12);
        }
    }
}


SettingsPanel::SettingsPanel(EngineAudioProcessor& ap):
PanelBase(ap),
open_audio_midi_settings_button(new igui::InstruoTextButtonElement("Audio/MIDI Settings...")),
osc_input_port_label("Input OSC Port"),
osc_input_port_number_editor(igui::TextField::InputType::Integer),
osc_output_port_label("Output OSC Port"),
osc_output_port_number_editor(igui::TextField::InputType::Integer),
min_max_section_heading("Slider Min/Max Settings", juce::Font::FontStyleFlags::bold),
param_column_name("Param"),
min_column_name("Min"),
max_column_name("Max"),
labels{},
min_fields{},
max_fields{}
{
    if (PlatformInspector::is_running_standalone())
    {
        this->addAndMakeVisible(this->open_audio_midi_settings_button);
    }
    this->open_audio_midi_settings_button.set_on_click_function([this]()
    {
        this->get_main_component().open_audio_midi_settings_window();
    });
    this->open_audio_midi_settings_button.view().set_highlight_colour(igui::colours::grey);
    this->open_audio_midi_settings_button.view_as<igui::InstruoTextButtonElement>()
        .set_text_justification(juce::Justification::centredLeft);

    this->addAndMakeVisible(this->osc_input_port_label);
    this->osc_input_port_label.set_justification(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->osc_input_port_number_editor);
    this->osc_input_port_number_editor.set_look_and_feel(new igui::InstruoLookAndFeel([this]{ return this->get_main_scale_factor(); }));
    this->osc_input_port_number_editor.set_text(juce::String(this->audio_processor.get_osc_interface().get_input_port_number()));
    this->osc_input_port_number_editor.set_on_change_function([this](const juce::String& text)
    {
        if (text.isEmpty())
        {
            this->indicate_osc_port_number_validity(this->osc_input_port_number_editor, false);
            return;
        }
        const int number = text.getIntValue();
        if (!idsp::is_between(number, 0, 65535))
        {
            this->indicate_osc_port_number_validity(this->osc_input_port_number_editor, false);
            return;
        }

        DBG("Setting OSC input port to " << number);
        this->audio_processor.get_osc_interface().set_input_port_number(number);
        this->indicate_osc_port_number_validity(this->osc_input_port_number_editor, true);
    });

    this->addAndMakeVisible(this->osc_output_port_label);
    this->osc_output_port_label.set_justification(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->osc_output_port_number_editor);
    this->osc_output_port_number_editor.set_look_and_feel(new igui::InstruoLookAndFeel([this]{ return this->get_main_scale_factor(); }));
    this->osc_output_port_number_editor.set_text(juce::String(this->audio_processor.get_osc_interface().get_output_port_number()));
    this->osc_output_port_number_editor.set_on_change_function([this](const juce::String& text)
    {
        if (text.isEmpty())
        {
            this->indicate_osc_port_number_validity(this->osc_output_port_number_editor, false);
            return;
        }
        const int number = text.getIntValue();
        if (!idsp::is_between(number, 0, 65535))
        {
            this->indicate_osc_port_number_validity(this->osc_output_port_number_editor, false);
            return;
        }

        DBG("Setting OSC output port to " << number);
        this->audio_processor.get_osc_interface().set_output_port_number(number);
        this->indicate_osc_port_number_validity(this->osc_output_port_number_editor, true);
    });

    this->addAndMakeVisible(this->min_max_section_heading);
    this->min_max_section_heading.set_justification(juce::Justification::centredLeft);

    this->addAndMakeVisible(this->param_column_name);
    this->param_column_name.set_justification(juce::Justification::centredLeft);
    this->addAndMakeVisible(this->min_column_name);
    this->min_column_name.set_justification(juce::Justification::centred);
    this->addAndMakeVisible(this->max_column_name);
    this->max_column_name.set_justification(juce::Justification::centred);

    for (size_t i = 0; i < this->audio_processor.float_params.size(); i++)
    {
        auto* param = this->audio_processor.float_params[i];

        this->labels.emplace_back(std::make_unique<igui::InstruoTextElement>(param->getName(32)));
        this->min_fields.emplace_back(std::make_unique<igui::TextField>(igui::TextField::InputType::Real));
        this->max_fields.emplace_back(std::make_unique<igui::TextField>(igui::TextField::InputType::Real));

        auto& label = *this->labels.back();
        this->addAndMakeVisible(label);
        label.set_justification(juce::Justification::centredLeft);

        auto& min_field = *this->min_fields.back();
        this->addAndMakeVisible(min_field);
        min_field.set_on_commit_function([this, i](const juce::String& text)
        {
            const auto number = text.getFloatValue();
            this->audio_processor.float_param_ranges[i].start = number;
            // Need to repaint affected slider for value display
            this->get_main_component().triggerAsyncUpdate();
        });
        min_field.set_look_and_feel(new igui::InstruoLookAndFeel([this]{ return this->get_main_scale_factor(); }));
        min_field.set_text(juce::String(this->audio_processor.float_param_ranges[i].start));

        auto& max_field = *this->max_fields.back();
        this->addAndMakeVisible(max_field);
        max_field.set_on_commit_function([this, i](const juce::String& text)
        {
            const auto number = text.getFloatValue();
            this->audio_processor.float_param_ranges[i].end = number;
            // Need to repaint affected slider for value display
            this->get_main_component().triggerAsyncUpdate();
        });
        max_field.set_look_and_feel(new igui::InstruoLookAndFeel([this]{ return this->get_main_scale_factor(); }));
        max_field.set_text(juce::String(this->audio_processor.float_param_ranges[i].end));
    }
}

void SettingsPanel::paint(juce::Graphics& g)
{
    this->draw_panel_background(g);
}

void SettingsPanel::bounds_changed()
{
    auto bounds = this->getLocalBounds().toFloat();
    bounds = bounds.reduced(15.37 * this->get_main_scale_factor());

    const float spacer_height = 7.05 * this->get_main_scale_factor();
    const float strip_height = 15.34 * this->get_main_scale_factor();
    const float text_height = 14 * this->get_main_scale_factor();

    if (PlatformInspector::is_running_standalone())
    {
        const auto strip_bounds = bounds.removeFromTop(strip_height);
        this->open_audio_midi_settings_button.setBounds(strip_bounds.toNearestInt());
        this->open_audio_midi_settings_button.view_as<igui::TextButtonElement>()
            .set_text_height(text_height);
        bounds.removeFromTop(spacer_height);
    }

    const auto add_editor_label_pair = [&bounds, strip_height, text_height, spacer_height]
        (igui::TextElement& label, igui::TextField& editor)
    {
        auto strip_bounds = bounds.removeFromTop(strip_height);
        editor.setBounds(strip_bounds.removeFromRight(bounds.getWidth() / 3).toNearestInt());
        label.setBounds(strip_bounds.toNearestInt());
        label.set_font_height(text_height);
        bounds.removeFromTop(spacer_height);
    };
    add_editor_label_pair(this->osc_input_port_label, this->osc_input_port_number_editor);
    add_editor_label_pair(this->osc_output_port_label, this->osc_output_port_number_editor);

    {
        bounds.removeFromTop(spacer_height);
        const auto strip_bounds = bounds.removeFromTop(strip_height);
        this->min_max_section_heading.setBounds(strip_bounds.toNearestInt());
        this->min_max_section_heading.set_font_height(text_height);
        bounds.removeFromTop(spacer_height);
    }

    const auto get_half_of_strip = [](juce::Rectangle<float>& strip) -> juce::Rectangle<int>
    {
        return strip.removeFromLeft(strip.getWidth() / 2).toNearestInt();
    };

    {
        auto strip_bounds = bounds.removeFromTop(strip_height);
        this->param_column_name.setBounds(get_half_of_strip(strip_bounds));
        this->min_column_name.setBounds(get_half_of_strip(strip_bounds));
        this->max_column_name.setBounds(strip_bounds.toNearestInt());
        bounds.removeFromTop(spacer_height);

        this->param_column_name.set_font_height(text_height);
        this->min_column_name.set_font_height(text_height);
        this->max_column_name.set_font_height(text_height);
    }

    jassert((this->labels.size() == this->min_fields.size()) && (this->labels.size() == this->max_fields.size()));
    for (size_t i = 0; i < this->labels.size(); i++)
    {
        auto strip_bounds = bounds.removeFromTop(strip_height);
        this->labels[i]->setBounds(get_half_of_strip(strip_bounds));
        this->min_fields[i]->setBounds(get_half_of_strip(strip_bounds).getProportion(juce::Rectangle<float>(0.1, 0.f, 0.8f, 1.f)));
        this->max_fields[i]->setBounds(strip_bounds.getProportion(juce::Rectangle<float>(0.1, 0.f, 0.8f, 1.f)).toNearestInt());

        bounds.removeFromTop(spacer_height);

        this->labels[i]->set_font_height(text_height);
        this->min_fields[i]->set_text_height(text_height);
        this->max_fields[i]->set_text_height(text_height);
    }
}

void SettingsPanel::indicate_osc_port_number_validity(igui::TextField& editor, bool is_valid)
{
    auto* raw_look_and_feel = editor.get_look_and_feel();
    auto* look_and_feel = dynamic_cast<igui::InstruoLookAndFeel*>(raw_look_and_feel);
    jassert(look_and_feel != nullptr);

    if (is_valid)
    {
        look_and_feel->set_text_editor_outline_colour(igui::colours::light_grey);
    }
    else
    {
        look_and_feel->set_text_editor_outline_colour(juce::Colours::red);
    }
    editor.sendLookAndFeelChange();
}
