#ifndef SYSTEM_EDITOR_H
#define SYSTEM_EDITOR_H

#include "JuceHeader.h"

#if JUCE_ANDROID
    #include "system/main_component_android.hpp"
    using PlatformMainComponent = MainComponentAndroid;
#else
    #include "system/main_component.hpp"
    using PlatformMainComponent = MainComponent;
#endif

namespace juce
{
    class StandaloneFilterWindow;
}

class EngineAudioProcessor;

class EngineAudioProcessorEditor :
    public juce::AudioProcessorEditor
{
public:
    EngineAudioProcessorEditor(EngineAudioProcessor&);

    void paint(juce::Graphics&) override;
    void resized() override;

    void set_using_native_title_bar(bool use_native);

    void open_audio_midi_settings();

    auto& get_main_component()
    {
        return this->main_component;
    }

private:
    juce::DocumentWindow* get_app_window_instance();

    juce::StandaloneFilterWindow* get_standalone_app_window();

    EngineAudioProcessor& audioProcessor;

    PlatformMainComponent main_component;
};

#endif
