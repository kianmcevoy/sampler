#include "system/editor.hpp"

#include "system/engine.hpp"

#include "juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h"

EngineAudioProcessorEditor::EngineAudioProcessorEditor(EngineAudioProcessor& ap):
    juce::AudioProcessorEditor(ap),
    audioProcessor{ap},
    main_component(ap, *this)
{
    addAndMakeVisible(this->main_component);
}

void EngineAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ignoreUnused(g);
}

void EngineAudioProcessorEditor::resized()
{
    this->main_component.setBounds(this->getLocalBounds());
}

void EngineAudioProcessorEditor::set_using_native_title_bar(bool use_native)
{
    auto* window = this->get_app_window_instance();
    if (window != nullptr)
    {
        const auto bounds = this->getBounds();
        window->setUsingNativeTitleBar(use_native);
        this->setBounds(bounds);
    }
}

void EngineAudioProcessorEditor::open_audio_midi_settings()
{
    auto* window = this->get_standalone_app_window();
    if (window != nullptr)
    {
        window->getPluginHolder()->showAudioSettingsDialog();
    }
}

juce::DocumentWindow* EngineAudioProcessorEditor::get_app_window_instance()
{
    juce::DocumentWindow* window = nullptr;
    juce::Component* comp = this;

    while (1)
    {
        window = dynamic_cast<juce::DocumentWindow*>(comp);
        if (window != nullptr)
        {
            break;
        }

        comp = comp->getParentComponent();
        if (comp == nullptr)
        {
            break;
        }
    }

    return window;
}

juce::StandaloneFilterWindow* EngineAudioProcessorEditor::get_standalone_app_window()
{
    juce::StandaloneFilterWindow* window = nullptr;
    juce::Component* comp = this;

    while (1)
    {
        window = dynamic_cast<juce::StandaloneFilterWindow*>(comp);
        if (window != nullptr)
        {
            break;
        }

        comp = comp->getParentComponent();
        if (comp == nullptr)
        {
            break;
        }
    }

    return window;
}
