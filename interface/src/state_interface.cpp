#include "interface/state_interface.hpp"
#include "interface/gui_data.hpp"


StateInterface::StateInterface(StateInterfaceOutputData& output)
{

}

void StateInterface::load(const StateInterfaceLoadData& loaded, StateInterfaceOutputData& output)
{

}

void StateInterface::process(const StateInterfaceInputData& input, StateInterfaceOutputData& output)
{
	for (size_t i = 0; i < max_voices; ++i)
	{
		output.gui.voice_active[i].store(input.state.voice_active[i]);
		output.gui.voice_position[i].store(input.state.voice_position[i]);
		output.gui.voice_volume[i].store(input.state.voice_volume[i]);
	}
}
