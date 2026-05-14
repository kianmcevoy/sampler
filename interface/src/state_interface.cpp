#include "interface/state_interface.hpp"

#include "interface/gui_data.hpp"

static_assert(StateData::max_playheads == GuiOutputData::max_playheads,
	"StateData and GuiOutputData must agree on max_playheads");

StateInterface::StateInterface(StateInterfaceOutputData& output)
{

}

void StateInterface::load(const StateInterfaceLoadData& loaded, StateInterfaceOutputData& output)
{

}

void StateInterface::process(const StateInterfaceInputData& input, StateInterfaceOutputData& output)
{
	for (size_t i = 0; i < StateData::max_playheads; ++i)
	{
		output.gui.playhead_active[i].store(input.state.playhead_active[i]);
		output.gui.playhead_position[i].store(input.state.playhead_position[i]);
		output.gui.playhead_volume[i].store(input.state.playhead_volume[i]);
	}
}
