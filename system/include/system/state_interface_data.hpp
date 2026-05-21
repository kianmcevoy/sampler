#ifndef SYSTEM_STATE_INTERFACE_DATA_H
#define SYSTEM_STATE_INTERFACE_DATA_H

#include "instrument/state_data.hpp"
#include "interface/utility_data.hpp"
#include "system/osc_control_data.hpp"
#include "interface/gui_data.hpp"

struct StateInterfaceInputData
{
    const StateData& state;
    const UtilityData& utility;
};

struct StateInterfaceLoadData
{
    const StateData& state;
};

struct StateInterfaceOutputData
{
    OscOutputData& osc;
    GuiOutputData& gui;
};

#endif
