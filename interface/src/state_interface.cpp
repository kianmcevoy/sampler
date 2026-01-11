#include "interface/state_interface.hpp"

StateInterface::StateInterface(StateInterfaceOutputData& output)
{

}

void StateInterface::load(const StateInterfaceLoadData& loaded, StateInterfaceOutputData& output)
{

}

void StateInterface::process(const StateInterfaceInputData& input, StateInterfaceOutputData& output)
{
	// Update GUI with current playback position
	output.gui.position.store(static_cast<int>(input.state.playback_position));
}
