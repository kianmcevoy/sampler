#include "system/control_builder.hpp"

void GuiControlBuilder::set_panel_size(float width, float height)
{
    this->panel_width = width;
    this->panel_height = height;
}

void GuiControlBuilder::set_display(float x, float y, float width, float height)
{
    this->display_geometry = {x, y, width, height};
}

void GuiControlBuilder::add_slider(const juce::String& panel, const juce::String& identifier, const juce::String& label,
                                   float x, float y, float w, float h)
{
    this->add_slider(panel, identifier, label, 0.f, 1.f, 0.f, x, y, w, h);
}

void GuiControlBuilder::add_slider(const juce::String& panel, const juce::String& identifier, const juce::String& label,
                                   float min, float max, float default_value,
                                   float x, float y, float w, float h)
{
    this->check_id(identifier);

    // The underlying JUCE parameter is always normalised [0, 1]; min/max only
    // affect display + ParameterInterface lookup. Convert the displayed
    // default to the equivalent normalised value.
    const float range_width = max - min;
    const float normalised_default = (range_width != 0.f)
        ? juce::jlimit(0.f, 1.f, (default_value - min) / range_width)
        : 0.f;

    this->parameters.add(std::make_unique<juce::AudioParameterFloat>(
        identifier, label,
        juce::NormalisableRange<float>(0.f, 1.f),
        normalised_default,
        juce::AudioParameterFloatAttributes{}
    ));

    this->slider_ids.emplace_back(identifier);
    this->parameter_ids.emplace_back(identifier);
    this->slider_ranges.emplace_back(min, max);
    this->control_geometries.push_back({x, y, w, h});
    this->control_panels.emplace_back(panel);
}

void GuiControlBuilder::add_button(const juce::String& panel, const juce::String& identifier, const juce::String& label,
                                   float x, float y, float w, float h)
{
    this->add_button(panel, identifier, label, false, x, y, w, h);
}

void GuiControlBuilder::add_button(const juce::String& panel, const juce::String& identifier, const juce::String& label,
                                   bool default_value,
                                   float x, float y, float w, float h)
{
    this->check_id(identifier);

    this->parameters.add(std::make_unique<juce::AudioParameterBool>(
        identifier, label,
        default_value,
        juce::AudioParameterBoolAttributes{}
    ));

    this->button_ids.emplace_back(identifier);
    this->parameter_ids.emplace_back(identifier);
    this->control_geometries.push_back({x, y, w, h});
    this->control_panels.emplace_back(panel);
}

void GuiControlBuilder::add_voice_button(const juce::String& panel, const juce::String& identifier, const juce::String& label,
                                         size_t voice_index,
                                         float x, float y, float w, float h)
{
    this->check_id(identifier);

    // Backed by an AudioParameterBool purely so the control sits in the same
    // parameter/geometry grid as everything else; the actual selection state
    // lives in GuiInputData and the visual state is driven by the audio
    // thread's voice activity (see VoiceButtonContainer in panels.cpp).
    this->parameters.add(std::make_unique<juce::AudioParameterBool>(
        identifier, label,
        false,
        juce::AudioParameterBoolAttributes{}
    ));

    this->voice_button_ids.emplace_back(identifier);
    this->voice_button_indices.emplace_back(voice_index);
    this->parameter_ids.emplace_back(identifier);
    this->control_geometries.push_back({x, y, w, h});
    this->control_panels.emplace_back(panel);
}

void GuiControlBuilder::add_trigger(const juce::String& panel, const juce::String& identifier, const juce::String& label,
                                    float x, float y, float w, float h)
{
    this->check_id(identifier);

    this->parameters.add(std::make_unique<juce::AudioParameterBool>(
        identifier, label,
        false,
        juce::AudioParameterBoolAttributes{}
    ));

    this->trigger_ids.emplace_back(identifier);
    this->parameter_ids.emplace_back(identifier);
    this->control_geometries.push_back({x, y, w, h});
    this->control_panels.emplace_back(panel);
}

void GuiControlBuilder::add_dropdown(const juce::String& panel, const juce::String& identifier, const juce::String& label,
                                     const juce::StringArray& options,
                                     float x, float y, float w, float h)
{
    this->check_id(identifier);

    this->parameters.add(std::make_unique<juce::AudioParameterChoice>(
        identifier, label,
        options,
        0,
        juce::AudioParameterChoiceAttributes{}
    ));

    this->dropdown_ids.emplace_back(identifier);
    this->parameter_ids.emplace_back(identifier);
    this->control_geometries.push_back({x, y, w, h});
    this->control_panels.emplace_back(panel);
}

juce::AudioProcessorValueTreeState::ParameterLayout&& GuiControlBuilder::transfer_parameter_layout()
{
    return std::move(this->parameters);
}

const std::vector<juce::String>& GuiControlBuilder::get_slider_identifiers() const
{
    return this->slider_ids;
}

const std::vector<juce::NormalisableRange<float>>& GuiControlBuilder::get_slider_ranges() const
{
    return this->slider_ranges;
}

const std::vector<juce::String>& GuiControlBuilder::get_button_identifiers() const
{
    return this->button_ids;
}

const std::vector<juce::String>& GuiControlBuilder::get_voice_button_identifiers() const
{
    return this->voice_button_ids;
}

const std::vector<size_t>& GuiControlBuilder::get_voice_button_indices() const
{
    return this->voice_button_indices;
}

const std::vector<juce::String>& GuiControlBuilder::get_trigger_identifiers() const
{
    return this->trigger_ids;
}

const std::vector<juce::String>& GuiControlBuilder::get_dropdown_identifiers() const
{
    return this->dropdown_ids;
}

const std::vector<juce::String>& GuiControlBuilder::get_all_parameter_identifiers() const
{
    return this->parameter_ids;
}

const std::vector<GuiControlBuilder::ControlGeometry>& GuiControlBuilder::get_control_geometries() const
{
    return this->control_geometries;
}

float GuiControlBuilder::get_panel_width() const
{
    return this->panel_width;
}

float GuiControlBuilder::get_panel_height() const
{
    return this->panel_height;
}

GuiControlBuilder::ControlGeometry GuiControlBuilder::get_display_geometry() const
{
    auto geometry = this->display_geometry;
    if (geometry.x < 0.f)
        geometry.x = (this->panel_width - geometry.w) * 0.5f;
    return geometry;
}

void GuiControlBuilder::check_id(const juce::String& identifier) const
{
    jassert(juce::Identifier::isValidIdentifier(identifier));

    jassert(std::find(this->parameter_ids.begin(), this->parameter_ids.end(), identifier) == this->parameter_ids.end());
}
