#include "system/gui_controls.hpp"
#include "instrument/constants.hpp"

#include <array>
#include <cstddef>

/**
 * @brief Use this function to build your control scheme.
 *
 * Start by calling `controls.set_panel_size(width, height)` to fix the GUI
 * canvas size (in design pixels — the rendered window scales this). Optionally
 * call `controls.set_display(x, y, w, h)` to position the waveform display
 * (top-left in design pixels); its waveform, cursors and start/end markers
 * scale with these bounds. Then add controls. Every `add_` method takes:
 *
 * - An `identifier` string, used to read the control in `ParameterInterface`,
 *   to save state (must be a valid XML tag), and as the OSC address.
 * - A `label` string used as the on-screen label.
 * - An `x, y` pair giving the top-left corner of the control in design pixels.
 * - Optional `w, h` (default 120×120).
 *
 * Note: the waveform display defaults to a 700x150 area centred horizontally
 * at y = 45..195. Use `set_display` to move/resize it; place controls so they
 * don't overlap.
 *
 * Available controls:
 *
 * `add_slider(id, label, x, y[, w, h])`
 *      Continuous-value knob, range [0:1] by default — adjustable at runtime
 *      in the Settings pane.
 * `add_slider(id, label, min, max, default, x, y[, w, h])`
 *      As above but with an explicit displayed range and default.
 * `add_button(id, label, x, y[, w, h])`
 *      Latching toggle switch.
 * `add_trigger(id, label, x, y[, w, h])`
 *      Momentary trigger button. Reads as `false` in every block except the
 *      one immediately after a click, when it reads `true`.
 * `add_dropdown(id, label, options, x, y[, w, h])`
 *      Multiple-choice menu. The control value in `ParameterInterface` is the
 *      index of the selected option.
 *
 * Accessing controls in `ParameterInterface::process`:
 * @code
 * float rate          = input.controls.sliders.at("lfo_rate");
 * bool  is_bipolar    = input.controls.buttons.at("lfo_bipolar");
 * bool  trigger_sync  = input.controls.triggers.at("lfo_sync");
 * size_t waveform_id  = input.controls.dropdowns.at("lfo_waveform");
 * @endcode
 *
 * Via OSC:
 * @code
 * const auto message = input.osc.messages.read();
 * if (message.id == "lfo_rate") rate = message.value.getFloat32();
 * @endcode
 */
void build_gui_control_scheme(GuiControlBuilder& controls)
{
	controls.set_panel_size(1600.f, 840.f);
	controls.set_display(450.f, 45.f, 700.f, 150.f);

	//transport + loop
	controls.add_trigger("load_sample", "Load Sample", 20.f, 215.f);
	controls.add_trigger("play",        "Play",        140.f, 215.f);
	controls.add_trigger("stop",        "Stop",        260.f, 215.f);
	controls.add_button ("loop",        "Loop",        380.f, 215.f);

	//playback parameters (start/length fractions of sample; speed bipolar [-4,+4])
	controls.add_slider("start",  "Start",   0.f,  1.f, 0.f,  20.f, 335.f);
	controls.add_slider("length", "Length",  0.f,  1.f, 1.f,  140.f, 335.f);
	controls.add_slider("speed",  "Speed",  -4.f,  4.f, 0.f,  260.f, 335.f);
	controls.add_slider("level",  "Level",   0.f,  1.f, 1.f,  380.f, 335.f);
	controls.add_slider("pan",    "Pan",     0.f,  1.f, 0.5f, 500.f, 335.f);

	//per-launch random deviation
	controls.add_slider("random_start",  "Random Start",  0.f, 1.f, 0.f, 20.f, 455.f);
	controls.add_slider("random_length", "Random Length", 0.f, 1.f, 0.f, 140.f, 455.f);
    controls.add_slider("random_speed",  "Random Speed",  0.f, 1.f, 0.f,  260.f, 455.f);
	controls.add_slider("random_level",  "Random Level",  0.f, 1.f, 0.f, 380.f, 455.f);
	controls.add_slider("random_pan",    "Random Pan",    0.f, 1.f, 0.f, 500.f, 455.f);

	//envelope routing depths, bipolar [-1, +1]
	controls.add_slider("envelope_start",  "Envelope Start",  -1.f, 1.f, 0.f, 20.f, 575.f);
	controls.add_slider("envelope_length", "Envelope Length", -1.f, 1.f, 0.f, 140.f, 575.f);
    controls.add_slider("envelope_speed",  "Envelope Speed",  -1.f, 1.f, 0.f, 260.f, 575.f);
	controls.add_slider("envelope_level",  "Envelope Level",  -1.f, 1.f, 0.f, 380.f, 575.f);
	controls.add_slider("envelope_pan",    "Envelope Pan",    -1.f, 1.f, 0.f, 500.f, 575.f);

    //playhead phase routing
    controls.add_slider("phase_start", "Phase Start", -1.f, 1.f, 0.f, 20.f, 695.f);
    controls.add_slider("phase_length", "Phase Length", -1.f, 1.f, 0.f, 140.f, 695.f);
    controls.add_slider("phase_speed", "Phase Speed", -1.f, 1.f, 0.f, 260.f, 695.f);
    controls.add_slider("phase_level", "Phase Level", -1.f, 1.f, 0.f, 380.f, 695.f);
    controls.add_slider("phase_pan", "Phase Pan", -1.f, 1.f, 0.f, 500.f, 695.f);

	//envelope controls
    controls.add_button("loop_envelope",  "Loop Envelope", 740.f, 215.f);
	controls.add_button("voice_stealing", "Steal/Protect", 860.f, 215.f);
	controls.add_button("envelope_sync",  "Sync Envelope", 980.f, 215.f);
    controls.add_trigger("envelope_trigger", "Trigger Envelope", 620.f, 215.f);
	controls.add_slider("time",   "Time", 0.f, 1.f, 1.f,  740.f, 335.f);
	controls.add_slider("skew",   "Skew", 0.f, 1.f, 0.5f, 860.f, 335.f);
	controls.add_slider("shape",  "Shape", 0.f, 1.f, 0.f, 980.f, 335.f);
    controls.add_dropdown("comp_source", "Comp Source", {"None", "Loop Phase", "Env Phase"}, 1100.f, 215.f);
    controls.add_slider("comp_threshold", "Comp Threshold", 0.f, 1.f, 0.5f, 1100.f, 335.f);

    //voice buttons
    static constexpr std::array<const char*, max_voices> voice_button_ids = {
        "voice_one", "voice_two", "voice_three", "voice_four",
        "voice_five", "voice_six", "voice_seven", "voice_eight",
    };
    static constexpr std::array<const char*, max_voices> voice_button_labels = {
        "1", "2", "3", "4", "5", "6", "7", "8",
    };
    for (size_t i = 0; i < max_voices; ++i)
    {
        controls.add_voice_button(
            voice_button_ids[i], voice_button_labels[i], static_cast<int>(i),
            620.f + 120.f * static_cast<float>(i), 695.f);
    }

    //global button
    controls.add_button("auto", "Auto", 1340.f, 215.f);
    controls.add_button("global", "Global", 1460.f, 215.f);

}
