#include "interface/state_interface.hpp"
#include "interface/gui_data.hpp"
#include "interface/voice_param_table.hpp"


StateInterface::StateInterface(StateInterfaceOutputData& output)
{

}

void StateInterface::load(const StateInterfaceLoadData& loaded, StateInterfaceOutputData& output)
{

}

void StateInterface::process(const StateInterfaceInputData& input, StateInterfaceOutputData& output)
{
	output.gui.playback_position_normalized.store(input.state.playback_position_normalized);

	for (size_t i = 0; i < max_voices; ++i)
	{
		output.gui.voice_active[i].store(input.state.voice_active[i]);
		output.gui.voice_position[i].store(input.state.voice_position[i]);
		output.gui.voice_volume[i].store(input.state.voice_volume[i]);
		output.gui.voice_layer[i].store(input.state.voice_layer[i]);

		// Mirror every per-voice live param into its atomic slot via the
		// voice_param_table — adding a new field needs no edits here.
		const auto& src = input.state.voice_live_params[i];
		auto& dst = output.gui.voice_params_snapshot[i];
		for (size_t fi = 0; fi < voice_param_floats.size(); ++fi)
			dst.floats[fi].store(src.*(voice_param_floats[fi].field));
		for (size_t bi = 0; bi < voice_param_bools.size(); ++bi)
			dst.bools[bi].store(src.*(voice_param_bools[bi].field));
	}

	// Per-layer aggregates for layer-view button rendering.
	for (size_t li = 0; li < max_layers; ++li)
	{
		output.gui.layer_has_active_voices[li].store(input.state.layer_has_active_voices[li]);
		output.gui.layer_summed_envelope[li].store(input.state.layer_summed_envelope[li]);
	}
}
