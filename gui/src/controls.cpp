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
 * - A `panel` string naming which user-facing panel hosts this control
 *   (e.g. "main", "modulation"). MainComponent's tab strip shows one tab
 *   per declared panel name and swaps which panel is visible underneath.
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
 * Available controls (each takes `panel` as its first argument):
 *
 * `add_slider(panel, id, label, x, y[, w, h])`
 *      Continuous-value knob, range [0:1] by default — adjustable at runtime
 *      in the Settings pane.
 * `add_slider(panel, id, label, min, max, default, x, y[, w, h])`
 *      As above but with an explicit displayed range and default.
 * `add_button(panel, id, label, x, y[, w, h])`
 *      Latching toggle switch.
 * `add_trigger(panel, id, label, x, y[, w, h])`
 *      Momentary trigger button. Reads as `false` in every block except the
 *      one immediately after a click, when it reads `true`.
 * `add_dropdown(panel, id, label, options, x, y[, w, h])`
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
	controls.add_trigger("main", "load_sample", "Load Sample", 20.f, 215.f);
	controls.add_trigger("main", "play",        "Play",        140.f, 215.f);
	controls.add_trigger("main", "stop",        "Stop",        260.f, 215.f);
	controls.add_trigger("main", "latch",       "Latch",       380.f, 215.f);
	controls.add_button ("main", "timestretch", "Time Stretch", true, 500.f, 215.f);

	// Recording — captures the audio input into the currently-selected layer's
	// 10-second buffer. record auto-stops at 10 s; stop_record ends early.
	// erase zeros the layer's buffer (and cancels any in-progress capture).
	// These are also bound to touch buttons in the Android UI.
	controls.add_trigger("main", "record",       "Record",       1460.f, 335.f);
	controls.add_trigger("main", "stop_record",  "Stop Record",  1460.f, 455.f);
	controls.add_trigger("main", "erase_layer",  "Erase",        1460.f, 695.f);

	//playback parameters (start/length fractions of sample; speed bipolar [-4,+4])
	controls.add_slider("main", "start",    "Start",    0.f,  1.f, 0.f,   20.f, 455.f);
	controls.add_slider("main", "length",   "Length",   0.f,  1.f, 1.f,  140.f, 455.f);
	controls.add_slider("main", "speed",    "Speed",   -4.f,  4.f, 1.f,  260.f, 455.f);
	controls.add_slider("main", "level",    "Level",    0.f,  1.f, 1.f,  380.f, 455.f);
	controls.add_slider("main", "pan",      "Pan",      0.f,  1.f, 0.5f, 500.f, 455.f);
	controls.add_slider("main", "position", "Position", 0.f,  1.f, 0.f,  620.f, 455.f);

	//per-launch random deviation
	controls.add_slider("modulation", "random_start",  "Random Start",  0.f, 1.f, 0.f,  20.f, 455.f);
	controls.add_slider("modulation", "random_length", "Random Length", 0.f, 1.f, 0.f, 140.f, 455.f);
    controls.add_slider("modulation", "random_speed",  "Random Speed",  0.f, 1.f, 0.f, 260.f, 455.f);
	controls.add_slider("modulation", "random_level",  "Random Level",  0.f, 1.f, 0.f, 380.f, 455.f);
	controls.add_slider("modulation", "random_pan",    "Random Pan",    0.f, 1.f, 0.f, 500.f, 455.f);

	//envelope routing depths, bipolar [-1, +1]
	controls.add_slider("modulation", "envelope_start",  "Envelope Start",  -1.f, 1.f, 0.f,  20.f, 575.f);
	controls.add_slider("modulation", "envelope_length", "Envelope Length", -1.f, 1.f, 0.f, 140.f, 575.f);
    controls.add_slider("modulation", "envelope_speed",  "Envelope Speed",  -1.f, 1.f, 0.f, 260.f, 575.f);
	controls.add_slider("modulation", "envelope_level",  "Envelope Level",  -1.f, 1.f, 1.f, 380.f, 575.f);
	controls.add_slider("modulation", "envelope_pan",    "Envelope Pan",    -1.f, 1.f, 0.f, 500.f, 575.f);
    controls.add_slider("modulation", "envelope_cutoff", "Envelope Cutoff", -1.f, 1.f, 0.f, 620.f, 575.f);
    controls.add_slider("modulation", "envelope_resonance", "Envelope Resonance", -1.f, 1.f, 0.f, 740.f, 575.f);

    //playhead phase routing
    controls.add_slider("modulation", "phase_start",  "Phase Start",  -1.f, 1.f, 0.f,  20.f, 695.f);
    controls.add_slider("modulation", "phase_length", "Phase Length", -1.f, 1.f, 0.f, 140.f, 695.f);
    controls.add_slider("modulation", "phase_speed",  "Phase Speed",  -1.f, 1.f, 0.f, 260.f, 695.f);
    controls.add_slider("modulation", "phase_level",  "Phase Level",  -1.f, 1.f, 0.f, 380.f, 695.f);
    controls.add_slider("modulation", "phase_pan",    "Phase Pan",    -1.f, 1.f, 0.f, 500.f, 695.f);

    //granular per-voice — all four are bipolar deviations centered on an
    //auto-computed C-OLA optimum. 0 = use the optimum, ±1 = deviate.
    //  pitch  : pitch shift in octaves (0 = no shift; up to ±2). Greyed out
    //           when timestretch is OFF.
    //  size   : grain length deviation (0 = auto; -1 = quarter; +1 = quadruple)
    //  shape  : window selection (0 = Hann-like Kaiser β=6; -1 = rect; +1 = Kaiser β=14)
    //  grains : grain count (-1 → 1 grain; 0 → C-OLA minimum; +1 → 8 grains)
    controls.add_slider("main", "pitch",  "Pitch",  -2.f, 2.f, 0.f,  20.f, 575.f);
    controls.add_slider("main", "size",   "Size",   -1.f, 1.f, 0.f, 140.f, 575.f);
    controls.add_slider("main", "shape",  "Shape",  -1.f, 1.f, 0.f, 260.f, 575.f);
    controls.add_slider("main", "grains", "Grains", -1.f, 1.f, 0.f, 380.f, 575.f);

    //granular per-grain random depth — 0 = identical grains, 0.5 = decorrelating
    //phase jitter (smoother sound), 1 = spray (chaotic). Curve is depth^4 -dominated.
    controls.add_slider("modulation", "random_pitch",    "Random Pitch",    0.f, 1.f, 0.f,  620.f, 455.f);
    controls.add_slider("modulation", "random_size",     "Random Size",     0.f, 1.f, 0.f,  740.f, 455.f);
    controls.add_slider("modulation", "random_shape",    "Random Shape",    0.f, 1.f, 0.f,  860.f, 455.f);
    controls.add_slider("modulation", "random_grains",   "Random Grains",   0.f, 1.f, 0.f,  980.f, 455.f);
    controls.add_slider("modulation", "random_position", "Random Position", 0.f, 1.f, 0.f, 1100.f, 455.f);
    controls.add_slider("modulation", "random_cutoff",   "Random Cutoff",   0.f, 1.f, 0.f, 1220.f, 455.f);

	//envelope controls (ADSR)
	controls.add_button ("main", "voice_stealing",    "Voice Stealing",    1220.f,  215.f);
    controls.add_trigger("main", "envelope_trigger", "Trigger Envelope",  1220.f,  335.f);
    controls.add_button("main", "scale_envelope", "Scale Envelope", 1340.f, 335.f);
	controls.add_slider ("main", "attack",  "Attack",  0.f, 1.f, 0.01f,  740.f,  335.f);
	controls.add_slider ("main", "decay",   "Decay",   0.f, 1.f, 0.1f,   860.f,  335.f);
	controls.add_slider ("main", "sustain", "Sustain", 0.f, 1.f, 0.8f,   980.f,  335.f);
	controls.add_slider ("main", "release", "Release", 0.f, 1.f, 0.3f,  1100.f,  335.f);

    //filter controls
    {
        juce::NormalisableRange<float> freq_range(20.f, 20000.f);
        freq_range.setSkewForCentre(1000.f);
        controls.add_slider("main", "filter_freq", "Filter Freq", freq_range, 5000.f, 980.f, 575.f);
    }
    controls.add_slider ("main", "filter_q",    "Filter Q",    0.5f, 30.f, 1.f,   1100.f,  575.f);

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
            "main", voice_button_ids[i], voice_button_labels[i], static_cast<int>(i),
            620.f + 120.f * static_cast<float>(i), 695.f);
    }

    //global button (default state when no voice is selected — also acts as
    //a "deselect voice / return to Global" button)
    controls.add_button("main", "global", "Global", 1460.f, 215.f);

    //"Stop All" kills every voice on every layer, regardless of mode/view.
    //Stop (above) is per-layer; this is the escape hatch.
    controls.add_trigger("main", "stop_all", "Stop All", 1340.f, 215.f);

    //View radio: voice view (default) shows per-voice activity; layer view
    //repurposes the voice buttons as layer selectors with per-layer LEDs.
    //Kept mutually exclusive by MainComponent.
    controls.add_button("main", "voice_view", "Voice View", true,  1340.f, 575.f);
    controls.add_button("main", "layer_view", "Layer View", false, 1460.f, 575.f);

    //markers — snap start/length to discrete marker positions. Type chooses
    //grid vs onset-detected transients; resolution picks how many markers (1..64).
    controls.add_button  ("main", "markers",     "Markers",     620.f, 215.f);
    controls.add_dropdown("main", "marker_type", "Marker Type",
                          juce::StringArray { "Time", "Transient" }, 740.f, 215.f);
    controls.add_slider  ("main", "resolution",  "Resolution",
                          1.f, 64.f, 8.f, 860.f, 215.f);

    //note routing — incoming MIDI note value drives pitch (default, octave
    //shift) or position (snap to marker / linear sample-fraction).
    controls.add_dropdown("main", "note_route", "Note Route",
                          juce::StringArray { "Pitch", "Position" }, 980.f, 215.f);

    //loop toggle (default on = current behaviour). When off, voices play once
    //through their start..end region and self-terminate at the end, regardless
    //of envelope stage.
    controls.add_button  ("main", "loop", "Loop", false, 1100.f, 215.f);

}
