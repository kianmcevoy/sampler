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
	controls.add_trigger("trigger", "Trigger");

	// Playback: start/length are fractions of the loaded sample [0, 1].
	// speed is bipolar [-4, +4]; default 0 (midpoint).
	controls.add_slider("start",  "Start",   0.f,  1.f, 0.f);
	controls.add_slider("length", "Length",  0.f,  1.f, 1.f);
	controls.add_slider("speed",  "Speed",  -4.f,  4.f, 0.f);
	controls.add_slider("level",  "Level",   0.f,  1.f, 1.f);
	controls.add_slider("pan",    "Pan",     0.f,  1.f, 0.5f);

	// Per-launch random deviation (fraction of each parameter's range).
	controls.add_slider("random_speed",  "Random Speed",  0.f, 1.f, 0.f);
	controls.add_slider("random_start",  "Random Start",  0.f, 1.f, 0.f);
	controls.add_slider("random_length", "Random Length", 0.f, 1.f, 0.f);
	controls.add_slider("random_level",  "Random Level",  0.f, 1.f, 0.f);
	controls.add_slider("random_pan",    "Random Pan",    0.f, 1.f, 0.f);

	// Envelope: time/spacing are fractions [0, 1]. skew bipolar around 0.5
	// (0=decay, 0.5=triangle, 1=ramp). spacing bipolar around 0.5
	// (<0.5=overlap, =0.5=no gap, >0.5=gap).
	// repeats displays 1..17; values 1..16 = count, the very top of the
	// slider engages Looping.
	controls.add_slider("time",    "Time",    0.f,  1.f, 1.f);
	controls.add_slider("skew",    "Skew",    0.f,  1.f, 0.5f);
	controls.add_slider("shape",   "Shape",   0.f,  1.f, 0.f);
	controls.add_slider("repeats", "Repeats", 1.f, 17.f, 1.f);
	controls.add_slider("spacing", "Spacing", 0.f,  1.f, 0.5f);


}
