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

		const auto& src = input.state.voice_live_params[i];
		auto& dst = output.gui.voice_params_snapshot[i];
		dst.start.store(src.start);
		dst.length.store(src.length);
		dst.speed.store(src.speed);
		dst.level.store(src.level);
		dst.pan.store(src.pan);
		dst.loop.store(src.loop);
		dst.time.store(src.time);
		dst.skew.store(src.skew);
		dst.shape.store(src.shape);
		dst.loop_envelope.store(src.loop_envelope);
		dst.envelope_sync.store(src.envelope_sync);
		dst.envelope_speed.store(src.envelope_speed);
		dst.envelope_start.store(src.envelope_start);
		dst.envelope_length.store(src.envelope_length);
		dst.envelope_level.store(src.envelope_level);
		dst.envelope_pan.store(src.envelope_pan);
	}
}
