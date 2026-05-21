#include "system/platform.hpp"

#include "JuceHeader.h"

bool PlatformInspector::is_running_standalone()
{
    const auto host_file = juce::File::getSpecialLocation(juce::File::SpecialLocationType::hostApplicationPath);
    const auto app_file = juce::File::getSpecialLocation(juce::File::SpecialLocationType::currentExecutableFile);

    return host_file == app_file;
}
