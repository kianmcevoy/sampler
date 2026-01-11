#include "system/gui_controls.hpp"

/**
 * @brief Use this function to build your control scheme.
 *
 * The GuiControlBuilder class has a number of `add_` methods for adding
 * controls to the layout. All of these take the following two arguments:
 *
 * - An `identifier` string, which you'll use you access the controls' values in
 * `ParameterInterface`. All the identifiers must be unique. This is also used
 * to save state so must be valid as an XML tag, and as the OSC address for
 * receiving values.
 *
 * - A `label` string, which is used by the GUI to label each control. These
 * don't need to be unique, but they should be easily distinguishable as the
 * only influence you have over GUI layout is the order in which each control is
 * displayed on the grid.
 *
 * The list of `add_` methods is as follows:
 *
 * `add_slider`
 *      Adds a continuous-value slider to the layout.
 *      By default this will have a range of [0:1], but this can be adjusted at
 *      runtime in the GUI's side/settings panel.
 * `add_button`
 *      Adds a latching toggle button/switch to the layout.
 * `add_trigger`
 *      Adds a trigger button to the layout.
 *      Triggers are boolean parameters that will always pass to
 *      `ParameterInterface::process` as `false`, except the DSP block after the
 *      button is clicked on when it will be passed in as `true`. It will then
 *      be `false` again in the next DSP block.
 * `add_dropdown`
 *      Adds a multiple choice dropdown menu to the layout.
 *      This takes an additional `options` argument: a list of strings which
 *      will be used to populate the dropdown menu in the GUI.
 *      The value of the dropdown in `ParameterInterface::process` is the index
 *      of the selected option string in the list.
 *
 * For example, an LFO's control layout might look like this:
 * @code
 * controls.add_slider("lfo_rate", "LFO Rate");
 * controls.add_button("lfo_bipolar", "Bipolar LFO");
 * controls.add_trigger("lfo_sync", "Sync LFO");
 * controls.add_dropdown("lfo_waveform", "LFO Waveform", {"Sine", "Triangle", "Square", "Saw", "Ramp"});
 * @endcode
 *
 * And accessing the controls in `ParameterInterface::process` might look like:
 * @code
 * float rate = input.controls.sliders.at("lfo_rate");
 * bool is_bipolar = input.controls.buttons.at("lfo_bipolar");
 * bool trigger_sync = input.controls.triggers.at("lfo_sync");
 * size_t waveform_id = input.controls.dropdowns.at("lfo_waveform");
 * @endcode
 *
 * And via OSC like:
 * @code
 * const auto message = input.osc.messages.read();
 * if (message.id == "lfo_rate")
 * {
 *     rate = message.value.getFloat32();
 * }
 * @endcode
 */
void build_gui_control_scheme(GuiControlBuilder& controls)
{
	controls.add_trigger("load_sample", "Load Sample");
	controls.add_trigger("play", "Play");
	controls.add_trigger("stop", "Stop");
	controls.add_button("loop", "Loop");
	
	controls.add_slider("start", "Start");
	controls.add_slider("length", "Length");
	controls.add_slider("speed", "Speed");
}
