#ifndef SYSTEM_OSC_CONTROL_DATA_H
#define SYSTEM_OSC_CONTROL_DATA_H

#include "idsp/ringbuffer.hpp"

#include "JuceHeader.h"

struct OscDataElement
{
    juce::String id;
    juce::OSCArgument value;

    template<class T>
    OscDataElement(const juce::String& id_, T value_):
    id{id_},
    value{value_}
    {}

    OscDataElement():
    id{},
    value{0}
    {}
};

struct OscInputData
{
    idsp::RingBufferReader<OscDataElement, 2048> messages;
};

struct OscOutputData
{
    idsp::RingBuffer<OscDataElement, 2048> messages;
};

#endif
