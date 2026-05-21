#ifndef SYSTEM_CONTROL_DATA_H
#define SYSTEM_CONTROL_DATA_H

#include "JuceHeader.h"

#include <vector>

struct GuiControlData
{
    std::unordered_map<juce::String, float> sliders;
    std::unordered_map<juce::String, bool> buttons;
    std::unordered_map<juce::String, bool> triggers;
    std::unordered_map<juce::String, size_t> dropdowns;
};

#endif
