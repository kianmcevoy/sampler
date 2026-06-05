# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A JUCE-based DSP prototyping framework, currently scaffolded as a polyphonic granular sampler instrument. Builds as a standalone app and a VST3 plugin. The repo is intended as a starting point for hardware-bound Instruo DSP projects, so it deliberately separates "instrument" (DSP) from "interface" (everything between the hardware/GUI/MIDI and the DSP) — see `README.md`.

## Build & run

CMake-based. The configured build directory is `build/` (in the workspace, not committed). Standard cycle:

```bash
cd build
cmake ..                       # configure (only needed when sources/CMakeLists.txt change)
cmake --build . -j$(nproc)     # incremental build
```

VS Code: press **F5** — the `build` task in `.vscode/tasks.json` runs `cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j$(nproc)` from `build/`, and the launch config attaches gdb (Linux) or lldb (macOS) to the Standalone app.

Output artefacts:
- Standalone: `build/sampler_artefacts/Debug/Standalone/sampler` (Linux/Windows) or `.../Standalone/sampler.app` (macOS)
- VST3: `build/sampler_artefacts/Debug/VST3/sampler.vst3`

[install.sh](install.sh) copies the VST3 into `~/instruo/plugins/` for hosting.

Note: `CMakeLists.txt` uses `file(GLOB ...)` for the source list. Adding or removing a `.cpp` requires a re-run of `cmake ..` before the next build.

No test suite is wired into this project.

## Repository layout — what's a submodule vs. what's yours

```
sampler/
├── instrument/         your DSP code — edit freely
├── interface/          your control-glue code (incl. MIDI parsing) — edit freely
├── gui/                your control-scheme declaration & assets — edit freely
├── system/             JUCE wrapper, panels, asset manager, MIDI plumbing — edit freely
│   └── JUCE/           SUBMODULE — JUCE 8
└── isl/                SUBMODULE — Instruo support library: idsp/ (DSP primitives), igui/ (UI), imidi/ (MIDI)
```

`system/` used to be a submodule (`JUCE-Prototyper-system`) but was absorbed into this repo (commit `fe88e4f`) because it had diverged too far to share — MIDI input, voice-button rendering, waveform-display tweaks, etc. Edit its files like any other source. Only `system/JUCE/` (and `isl/`) remain as submodules.

The old `idsp` and `system/igui` submodules were merged into `isl/` long before that. **Do not** assume `system/igui/` or top-level `idsp/` exist.

`CLAUDE.md` files in the remaining submodules (if added) are loaded automatically by Claude Code — keep project-level guidance here in the root.

## High-level architecture

Five classes you actually touch. Data flows in a clear cycle each audio block:

```
GUI controls ──┐
MIDI buffer ───┼─► ParameterInterface ──► Instrument ──► StateInterface ──► GUI display
OSC input ─────┘    (ParameterData +       (VoicePool +    (StateData →      (waveform,
                     MidiNoteEvents)        StateData)      GuiOutputData)    voice LEDs,
                                                                              live params)
```

1. **`gui/src/controls.cpp` :: `build_gui_control_scheme(GuiControlBuilder&)`** — declarative control layout. Each entry has a unique identifier (used as XML tag, OSC address, and JUCE parameter ID) plus a display label, panel name, and geometry.
2. **`ParameterInterface::process`** ([interface/src/parameter_interface.cpp](interface/src/parameter_interface.cpp)) — reads `GuiControlData`, parses the `juce::MidiBuffer`, reads queued OSC, conditions everything into `ParameterData` for the instrument. Also performs file I/O and publishes display data to `GuiOutputData`.
3. **`Instrument::process`** ([instrument/src/instrument.cpp](instrument/src/instrument.cpp)) — pure DSP. Reads `ParameterData` + `layer_buffers[]`, runs the 8-voice pool, writes audio to `PolyDspBuffer` and state to `StateData`. Stays MIDI/GUI-agnostic: it just sees `play`/`stop`/`midi_events` as different trigger sources.
4. **`StateInterface::process`** ([interface/src/state_interface.cpp](interface/src/state_interface.cpp)) — copies per-voice `StateData` (active flags, positions, live-param snapshots) into atomic `GuiOutputData` fields for the GUI thread to read.
5. **`MainComponent`** ([system/src/main_component.cpp](system/src/main_component.cpp)) — GUI-thread coordinator. Runs a 100 ms timer that watches `GuiOutputData` flags (file-loaded, voice-active LEDs) and writes `GuiInputData` (selected_voice, global_mode, file path, selected_layer). Also owns the Voice / Global mode state machine driven by the voice buttons, delegated to `ModeController`.

**All of process 1–4 runs on the audio thread.** The GUI thread only reads `GuiOutputData` and writes `GuiInputData`. There is no locking — communication uses `std::atomic` for primitives and the "publish via atomic flag" idiom for bulk data (waveform vectors).

## ParameterInterface — control-source abstraction

The class's job is to turn *any* control source (GUI sliders, MIDI, OSC) into a single `ParameterData` snapshot that the Instrument consumes without caring where it came from. This is the boundary that makes the Instrument hardware-agnostic.

Three input channels per block:
- **`GuiControlData`** — slider/button/trigger/dropdown values, keyed by control identifier.
- **`juce::MidiBuffer`** — raw MIDI from the host, parsed via `imidi::MessageQueueStatic<256>`.
- **`OscInputData`** — queued OSC messages keyed by control identifier (same string as the sliders).

Outputs into `ParameterData`:
- Slider/button values (level, speed, start, length, etc.) — direct copies.
- `start` is the sum of the start slider and the latched modwheel position, clamped to [0, 1].
- When markers are enabled, `start` / `length` are quantised onto marker positions.
- `midi_events` array — per-block note-on/note-off events with pre-computed velocity-scaled level and pitch-scaled ratio.

## MIDI subsystem

MIDI parsing, the note stack, and all CC routing live inside `ParameterInterface`. The Instrument never sees `juce::MidiBuffer` — it consumes a fixed-size queue of `MidiNoteEvent` structs.

### Parsing
`isl/include/imidi/` (header-only) provides `imidi::MessageQueueStatic<N>`. Each block, raw bytes from every `juce::MidiMessageMetadata` are pushed in via `from_bytes`, then drained as typed `imidi::Message` events (`NoteOn`, `NoteOff`, `ControlChange`, …).

### Note-on / note-off
- **Note-on**: pick a slot in `active_notes_` (first inactive, or slot 0 if stealing enabled), assign a monotonic `midi_seq_counter_`, compute:
  - `velocity_factor = (velocity / 127)^2`  — squared (quasi-perceptual) curve. The Voice multiplies its per-grain level by this; it is **not** baked into VoiceLiveParams.
  - `note_ratio = 2^((note − 60) / 12)`  — pure pitch ratio (no slider speed baked in). The Voice combines it with slider speed/pitch inside `set_live_params`.
  - Emit `MidiNoteEvent { note_on=true, midi_seq, velocity, note_ratio, note_number }`.
- **Note-on velocity 0** is treated as note-off (running-status convention).
- **Note-off**: find the `active_notes_` slot matching the note number, emit `MidiNoteEvent { note_on=false, midi_seq }`, clear the slot.

### Voice routing on the Instrument side
The Instrument tracks per-voice MIDI ownership in `voice_midi_seq_[slot]`. On each note-on event it allocates a voice via the same `VoiceAllocator` the play button uses (respecting `voice_stealing`), triggers it **gated** (ADSR holds at sustain), records `voice_midi_seq_[slot] = ev.midi_seq`, and calls `set_midi_offsets(octave_offset, velocity_factor)`. On each note-off it walks the voices to find the matching seq and calls `voice.release()` (which begins the release ramp from the current envelope value). At the top of every block it clears `voice_midi_seq_[s]` for any voice that has become inactive, so a recycled slot can't be matched by a stale note-off.

`ParameterInterface` does **not** predict which slot the allocator will pick — it just hands out `midi_seq` and lets the Instrument route. This is what keeps the Instrument's allocator policy authoritative.

### Latch
A `latch` trigger marks every still-gated MIDI voice as latched. The next matching note-off is silently swallowed; the voice keeps looping until `stop` (or `stop_all`) kills it. Voices already in release are skipped.

### Note routing (Pitch / Position)
Set by the `note_route` dropdown:
- **Pitch** (default) — the note number drives pitch via `set_midi_offsets(log2(note_ratio), velocity_factor)`. The voice's effective speed/pitch combines slider values with this.
- **Position** — the note number picks a start fraction (snapped to a marker when markers are active, else `note / 127`). Pitch is suppressed (octave offset = 0). Length is re-anchored from the new start, so a note plays a marker-bounded region rather than transposing.

### CC routing
- **CC1 (Modwheel)** — latched into `ParameterInterface::modwheel_position_` and summed with the start slider. Persists across blocks.

Other CCs are ignored. Add them in `ParameterInterface::process_midi`.

### MIDI + play button coexist
MIDI voices and play-button voices share the voice pool. The play button still works independently — pressing it allocates a new voice via the allocator (no envelope, just plain looping playback). The stop button kills voices on the current layer; `stop_all` kills every voice on every layer.

## Envelope (ADSR / AR)

`idsp::Envelope` (lives in `instrument/include/instrument/envelope.hpp`, will eventually migrate into `isl/include/idsp/`) is a classic ADSR envelope with two entry points:

- `trigger_ar(attack, release)` — Attack → Release. No sustain. Used by the `envelope_trigger` button. Attack-end falls straight into Release.
- `trigger_adsr(attack, decay, sustain, release)` — Attack → Decay → Sustain (held) → Release on `release()`. Used by MIDI note-on; the matching note-off calls `release()`. Sustain is a level in [0, 1]; the held phase persists indefinitely.
- `release()` begins the release ramp from the *current* value, so an early note-off during Attack or Decay produces a clean ramp-down from the partial value (no jump to peak first).
- Curves are linear A, linear D, quadratic-exponential R.
- `get_phase()` returns lifetime [0, 1] across A+D+R for AR/ADSR. During Sustain it sits at the D→R boundary so phase-driven modulation has a stable value while the note is held.

A/D/R slider values map linearly to [0, 5] s at the current sample rate (see `Voice::set_live_params::resolve_dur`).

## Voice pool, allocator, stealing

`max_voices = 8`. `VoicePool` is a plain `std::array<Voice, 8>` with a `kill_all()` helper.

`VoiceAllocator::acquire(pool, voice_stealing, preferred_slot = -1)`:
- If `preferred_slot ∈ [0, 8)` → return that exact slot (used by Voice mode to force a launch into the selected slot).
- Else if any inactive voice exists → return the first inactive.
- Else if `voice_stealing` → return the active voice with the smallest `launch_seq_`.
- Else → `nullptr` (caller silently drops).

Voices have direction *locked at trigger* (`forward_ = speed >= 0`). Modulation of speed magnitude is allowed; sign is not. The base parameters (start, length, level, pan, env durations, mod depths, granular deviations) are kept live-editable via `set_live_params` once per block. The running envelope keeps its original attack/decay/release counters until the next retrigger.

## Layers — 8 independent sample slots

`max_layers = 8`. Each layer owns a `SampleBuffer` (display PolyDspBuffer + a pair of `idsp::LagrangeDelay<524288>` playback buffers + cached transient indices). Voices and layers are decoupled: the 8 voices are a global pool, and each voice records the layer index it was triggered on (`set_layer(current_layer)`). For its lifetime, the voice reads from `layer_buffers[v.layer()]` regardless of what layer the user later switches to.

GUI-side, the "layer view" radio repurposes the 8 voice buttons as layer selectors. `selected_layer` (in `GuiInputData`) drives:
- Which buffer the file-chooser loads into.
- Which buffer's waveform / markers the GUI displays.
- Which layer new triggers (`play`, MIDI, `envelope_trigger`) tag voices with.

Per-layer `stop` and Global-mode operations only touch voices on the editing layer; `stop_all` is the cross-layer kill switch.

## Two-mode interaction model — Voice / Global

Driven by the voice buttons + Global button. State lives in `MainComponent` (GUI thread), delegated to `ModeController` ([interface/src/mode_controller.cpp](interface/src/mode_controller.cpp)). It publishes to the audio thread via `gui_input.selected_voice` (atomic int, -1 = no selection) and `gui_input.global_mode` (atomic bool). `ParameterInterface` copies these into `parameter.selected_voice` and `parameter.global_mode` each block.

The Instrument resolves this into a two-state mux at the top of `process()`:

- **Voice mode** (`selected_voice ∈ [0, 8)` and that slot is active)
  - Slider edits overlay onto the selected voice each block via `overlay_live_params()` (no random jitter).
  - `play` forces a launch into the selected slot.
  - `stop` kills only the selected voice.
  - GUI snaps sliders to the selected voice's `voice_params_snapshot` when entered; restores the pre-entry slider state on exit.
- **Global mode** (default; no voice selected)
  - For the **5 playback params** (start, length, speed, level, pan): scaling-pickup. Each on-layer active voice gets its trigger-time `(slider, voice_value)` anchor; while a slider moves, every voice's value rescales piecewise-linearly through its anchor toward the slider's min/max. Returning the slider to the anchor restores the original (post-random) value; pushing to either extreme collapses every voice onto the slider value.
  - For the **rest** (envelope, phase, granular, deviations): direct overlay onto every on-layer active voice.
  - `play` allocates a new voice (does not retrigger existing voices).
  - `stop` kills every voice on the editing layer.
  - Voices on layers other than the editing layer are not touched by slider edits or `stop`. They keep playing whatever was last pushed into them (cached in `voice_effective_params_`).

Clicking the same voice button twice exits Voice mode → Global mode. Clicking Global exits Voice mode. Switching to Layer view force-deselects any selected voice. The two modes are mutually exclusive — invariant enforced by `ModeController`.

## Position scrubbing

The `position` slider is bidirectional: the GUI's 100 ms timer reads `playback_position_normalized` and writes it back as the slider's value (so the slider follows the audio), while the user can grab and drag it.

The audio thread distinguishes "echo" from "user drag" via the `position_scrubbing` flag (GUI sets it from `parameter_being_gestured()`). While scrubbing:
- **Voice mode** → jump-to on the selected voice.
- **Global mode** → value-scaling pickup, scoped to the current layer. On the rising edge, snapshot the slider value and each in-layer voice's current loop position; while the drag continues, rescale each voice's position through its own anchor. Off-layer voices untouched.
- **Neither** → jump-to on the newest active voice on the current layer.

## Per-voice live parameters

`VoiceLiveParams` is the live-editable subset of `ParameterData`. Each Voice holds one copy; the Instrument pushes the current snapshot into the voice via `Voice::set_live_params()` each block, so slider edits to the live-edit target (Voice mode) or to all on-layer voices (Global mode) take effect immediately.

The `voice_param_table` ([interface/include/interface/voice_param_table.hpp](interface/include/interface/voice_param_table.hpp)) is the single source of truth for which `VoiceLiveParams` fields are mirrored to the GUI for snap-on-select. Adding a new per-voice editable param is:
1. Add the field to `VoiceLiveParams`.
2. Add a row to `voice_param_floats` (or `voice_param_bools`).
3. Wire the slider in `gui/src/controls.cpp` and read it in `ParameterInterface`.

Global params (`random_*`, etc — apply only at trigger time) live ONLY in `ParameterData` and do NOT go into `voice_param_table`.

At trigger time, the Instrument writes the **effective** post-random params into `voice_live_params_[slot]` via `build_effective_live_params()`. So the slot reflects "what this voice is actually playing" (with jitter baked in), which is what the GUI snaps to when you select the voice. The `voice_effective_params_[slot]` array additionally remembers what was last pushed into each voice via `set_live_params`, so off-layer voices in Global mode don't snap back to their trigger value when the user wanders to another layer.

## Granular engine (timestretch mode)

When `timestretch` is ON, each voice runs a constant-overlap-add (C-OLA) granular cluster, allowing pitch and time to move independently:
- **Position** advances at `speed` per output sample (the loop-traversal rate).
- **Pitch** is controlled by `pitch_deviation` (octaves), which sets each grain's per-sample read rate via `pitch_ratio_ = 2^(pitch_deviation + midi_octave_offset)`.
- **Grain count** is `overlap_eff_` (1..8), derived from `grains_deviation`.
- **Grain length** is `n_eff_samples_` (clamped 20..200 ms), derived from `size_deviation`.
- **Window shape** is a blend of two adjacent Kaiser-β LUTs ({0, 3, 6, 10, 14}), selected from `shape_deviation`.

A new grain is spawned every `n_eff_samples_ / overlap_eff_` *output* samples (NOT scaled by speed, so cluster geometry stays constant on the output side). Each grain carries per-grain jitter for pitch / size / shape / position / level / pan / grain-hop, driven by the `random_*` depth controls.

When `timestretch` is OFF, the voice runs in **loop-crossfade mode**: a single playhead reads at `speed` (pitch tracks speed exactly), and crossfades on itself at the loop boundary. The Body grain becomes a FadeOut while a new FadeIn grain spawns at the start of the loop region; the crossfade is half the effective window length.

`Voice::current_level()` returns `envelope_value × base_level` for active voices (used by the voice-button LED brightness aggregation in `layer_summed_envelope[]`).

## Markers (Time / Transient)

The `markers` button enables marker snapping. The `marker_type` dropdown selects between:
- **Time** — evenly spaced grid of N markers across the buffer (N = `resolution` slider, 1..64).
- **Transient** — onset-detected positions cached in `SampleBuffer::transient_indices` at load time. N = min(resolution, transient_count).

When enabled:
- The `start` slider picks a marker index; the resulting `start` fraction is published in `ParameterData`.
- The `length` slider picks a marker count; the end fraction is the next-marker-after-the-span (or 1.0 if it overruns).
- `marker_fractions[]` is published so the Instrument's per-launch random jitter can quantise onto marker positions instead of producing continuous values.

Transient detection lives in [instrument/include/instrument/onset.hpp](instrument/include/instrument/onset.hpp): HPF → rectify → fast-attack / slow-release envelope follower → positive first difference → adaptive threshold → 30 ms cluster suppression → zero-crossing rewind → top-64 by strength, sorted by time.

## Cross-thread protocol

`GuiOutputData` (audio → GUI) and `GuiInputData` (GUI → audio) in [interface/include/interface/gui_data.hpp](interface/include/interface/gui_data.hpp) are the only legitimate channel. The handshake for file loading is the canonical pattern:

1. Audio thread sets `gui.request_file_chooser = true` on Load Sample.
2. GUI thread sees the flag, opens `juce::FileChooser`, writes `gui_input.sample_file_path`, sets `gui_input.file_path_ready = true`, clears the request.
3. Audio thread sees `file_path_ready`, loads synchronously inside `ParameterInterface::process` (into the currently-selected layer), publishes waveform via `gui.waveform_left/right`, sets `gui.waveform_ready = true` and `gui.file_loaded = true`.
4. GUI thread sees `file_loaded`, clears the flags.

Bulk data (`waveform_left/right`, `voice_params_snapshot`) is only consumed after a release-acquire fence on an atomic flag.

## Sample buffer / playback model

`SampleBuffer` ([instrument/include/instrument/audio_data.hpp](instrument/include/instrument/audio_data.hpp)) holds:
- `PolyDspBuffer loaded_sample` — dynamic-size, source of truth for the waveform; also what the GUI display reads and what onset detection runs on.
- `std::array<idsp::LagrangeDelay<524288>, 2> sample` — static-size playback buffers with 4th-order Lagrange interpolation, indexed by absolute fractional position via `read_at(float position)`.
- `transient_indices[]` / `transient_count` — cached onset positions for marker mode.

`LagrangeDelay` (and `LinearDelay` / `AllpassDelay` / `WindowedDelay`) in `isl/include/idsp/delay.hpp` is fundamentally a delay line but the `read_at` method treats internal storage as a generic interpolated buffer. This is how the sampler reads at fractional indices for arbitrary playback speeds.

When loading, `ParameterInterface` writes the audio into both `loaded_sample` (display) and the `sample[]` LagrangeDelays (playback) — these must stay in sync. `num_samples` is capped at 524288 (LagrangeDelay capacity). Each of the 8 layers has its own `SampleBuffer`, so layered playback can pull from independent samples.

## GUI control layout

Defined in [gui/src/controls.cpp](gui/src/controls.cpp) via the `GuiControlBuilder` API. Controls are placed by explicit (x, y) on a 1600 × 840 canvas; the waveform display sits at (450, 45) with size 700 × 150. Controls are grouped into named "panels" (currently `main` and `modulation`) — `MainComponent`'s tab strip swaps which panel is visible.

| Group | Identifiers | Type |
| --- | --- | --- |
| Transport | `load_sample`, `play`, `stop`, `stop_all`, `latch`, `loop`, `timestretch`, `record`, `stop_record`, `erase_layer` | triggers + buttons |
| Playback | `start`, `length`, `speed`, `level`, `pan`, `position` | sliders |
| Granular | `pitch`, `size`, `shape`, `grains` | sliders (bipolar) |
| Per-launch random | `random_start/length/speed/level/pan` | sliders |
| Per-grain random | `random_pitch/size/shape/grains/position` | sliders |
| Envelope mod depths | `envelope_start/length/speed/level/pan` | sliders (bipolar [−1, +1]) |
| Phase mod depths | `phase_start/length/speed/level/pan` | sliders (bipolar [−1, +1]) |
| Envelope | `attack`, `decay`, `sustain`, `release`, `envelope_trigger`, `voice_stealing` | sliders + trigger + button |
| Markers | `markers`, `marker_type` (dropdown), `resolution`, `note_route` (dropdown) | mixed |
| Voice select | `voice_one` … `voice_eight` | voice_buttons (carry slot index 0..7) |
| Mode / view | `global`, `voice_view`, `layer_view` | buttons |

### GuiControlBuilder methods

All `add_*` methods take a `panel` string as the first argument.

- `set_panel_size(w, h)` / `set_display(x, y, w, h)` — canvas + waveform geometry.
- `add_slider(panel, id, label, x, y, w=120, h=120)` — slider with default [0, 1] range.
- `add_slider(panel, id, label, min, max, default, x, y, w, h)` — slider with explicit display range. The underlying JUCE param is normalized [0, 1]; `ParameterInterface` reads the *scaled* value.
- `add_button(panel, id, label, …)` — latching toggle.
- `add_voice_button(panel, id, label, voice_index, …)` — voice select. LED driven by `voice_active[voice_index]` (or layer aggregates when layer view is on); click is interpreted by `MainComponent` (delegated to `ModeController`).
- `add_trigger(panel, id, label, …)` — momentary; reads `true` only on the block immediately after the click.
- `add_dropdown(panel, id, label, options, …)` — choice menu; `ParameterInterface` reads the selected index.

### Adding a new control

1. Add the line to `build_gui_control_scheme` in [gui/src/controls.cpp](gui/src/controls.cpp).
2. Read it in `ParameterInterface::process` via `input.controls.sliders.at("id")`, `.buttons.at("id")`, `.triggers.at("id")`, or `.dropdowns.at("id")`.
3. Add a field to `ParameterData` if the instrument needs it; assign in ParameterInterface, consume in `Instrument::process`. For per-voice live editability, also add to `VoiceLiveParams` and register in `voice_param_table.hpp` (if the GUI should snap to it).

## State publication

`StateData` ([instrument/include/instrument/state_data.hpp](instrument/include/instrument/state_data.hpp)) is what the Instrument writes each block:

- `playback_position` — float, the first active voice's position, or -1.0 if none.
- `playback_position_normalized` — float [0, 1], the routing-target voice's loop position, for the bidirectional `position` slider.
- `voice_active[8]` / `voice_position[8]` / `voice_volume[8]` / `voice_layer[8]` — per-voice flags / positions / levels / layer indices.
- `voice_live_params[8]` — per-voice effective live-param snapshot (post-random for fresh launches; live-edited for selected/global voices).
- `layer_has_active_voices[8]` / `layer_summed_envelope[8]` — per-layer aggregates for the layer-view voice-button LEDs.

`StateInterface::process` copies these into the atomic mirrors in `GuiOutputData`: per-voice fields directly, and `voice_live_params` field-by-field via `voice_param_table` into `voice_params_snapshot[]`. The GUI consumes the snapshots when the user selects a voice — that's how the sliders snap to the voice's currently-playing values (including its random offset).

## OSC

`ParameterInterface::process` also receives `OscInputData` with queued OSC messages keyed by control identifier (same string as the slider/button). Useful for external control without going through the GUI. Add new routings inside `parameter_interface.cpp`.

## Things that have bitten us

- **JUCE 8 animation module is required.** `igui::elements` uses `juce::Animator` / `juce::VBlankAnimatorUpdater`, which live in the separate `juce_animation` module — already wired into `CMakeLists.txt`'s `target_link_libraries`.
- **`isl/include/idsp/delay.hpp` historically didn't include `idsp/buffer_types.hpp`** despite using `idsp::SampleBufferStatic<N>`. If you see a "does not name a template type" error for `SampleBufferStatic`, check the include order in `instrument/include/instrument/audio_data.hpp` (it includes `buffer_types.hpp` first as a guard).
- **`system/` is no longer a submodule** — it was absorbed into this repo (commit `fe88e4f`) because the project-specific tweaks (MIDI buffer plumbing in `processBlock`, voice-button rendering, grain → playhead rename, panels.cpp waveform-display additions) had diverged too far from the shared template. Edit its files like any other source. The pre-absorption history lives in `JUCE-Prototyper-system` on the `sampler` branch if you ever need to compare.
- **Font lifetime**: `igui::initialise_instruo_font(...)` returns a `unique_ptr<FontLifetimeManager>` that must be stored in `MainComponent` *before* any panel construction and destroyed *after* them. Field declaration order in `main_component.hpp` matters.
- **Audio-thread file I/O**: `ParameterInterface` loads sample files synchronously inside `process()`, including onset detection. This is acceptable for prototyping but blocks the audio thread for the duration of the file read — fine on desktop, won't fly on hardware. To be moved to a worker thread eventually.
- **MIDI velocity 0 is note-off**: don't forget the running-status convention when adding new MIDI handlers — JUCE delivers velocity-0 NoteOn messages literally, and `ParameterInterface::process_midi` routes them through `emit_note_off`. If you add code that reads NoteOn velocity directly, replicate that check.
- **`file(GLOB ...)` in CMakeLists.txt**: adding or removing a `.cpp` requires re-running `cmake ..` before the next build — the glob result is cached at configure time.

## Recording (per-layer, 10 s)

Each of the 8 layers can capture audio from the host input. State lives in `ParameterInterface`:

- `recording_layer_` — -1 idle, else 0..max_layers-1 capturing.
- `recording_sample_pos_` — sample count into the buffer.
- `max_record_samples_` — `sample_rate * 10` (capped at LagrangeDelay capacity 524288).

The GUI fires REC / STOP / ERASE by setting `GuiInputData::record_start_request` / `record_stop_request` / `erase_request` atomics. ParameterInterface drains them at the end of `process()` with `exchange(false)`. Status flows back through `GuiOutputData::is_recording` and `record_progress`.

The capture flow:
1. **start_recording**: zero the target layer's `loaded_sample`, reset the `LagrangeDelay` pair, set `p.stop = true` for the block (so voices already on that layer die before reading mutated memory).
2. **process_recording** (each block while active): copy the input audio block into `loaded_sample.channel(0)/channel(1)`. Advance the write head. Auto-call stop_recording at 10 s.
3. **stop_recording**: replay the captured `loaded_sample` into both LagrangeDelays (the only way to populate them — they're write-only append). Re-run onset detection. Republish waveform if the recorded layer is the displayed layer.
4. **erase_layer_buffer**: zero `loaded_sample` (resize to 0), reset LagrangeDelays, clear transient cache, clear waveform if this is the displayed layer.

ParameterInterface needs the audio input + sample rate, threaded through `ParameterInterfaceInputData::audio` (a `const PolyDspBuffer&`) and `sample_rate` (a float). EngineAudioProcessor wires both in `processBlock`.

## Touch trigger events (Android UI)

To allow the Android `TouchWaveformView` to launch voices at specific (start, level) without disturbing the slider state, ParameterData gained a `touch_events[]` array (`max_touch_events_per_block = max_voices`) alongside `midi_events[]`. Each event carries `(start_fraction, level, target_layer)`. The Instrument processes them right after the `play` block — it allocates a voice via the standard allocator, overrides `voice_live_params_[slot].start` / `.level` with the event values, and trigger_plain's into the target layer's buffer.

The GUI side uses a 16-deep single-producer/single-consumer ring in `GuiInputData`:
- `touch_event_queue[16]` of `PendingTouchEvent`.
- `touch_event_write_idx` (GUI writes via atomic with release semantics).
- `touch_event_read_idx`  (audio reads via atomic with acquire semantics).

For per-voice touch scrubbing (touch-and-drag on an existing playhead), `GuiInputData` gained `voice_scrub_slot / voice_scrub_position / voice_scrub_level` atomics. ParameterInterface copies them into `p.touch_scrub_*` each block. The Instrument:
- Teleports `voices_[touch_scrub_slot]` to `touch_scrub_position` (overrides any position-slider scrub for that slot).
- In the per-voice live-edit loop, overrides `effective.level` with `touch_scrub_level` for the same slot, regardless of Voice/Global mode.

Only one voice may be scrubbed at a time (newest grab wins). Multi-touch voice triggering is unrestricted.

## Android target

The Android build lives in [android/](android/) and is driven by Gradle 8.11 + Android Gradle Plugin 8.4. It produces `android/app/build/outputs/apk/debug/app-debug.apk` via:

```bash
cd android
JAVA_HOME=$HOME/android-studio/jbr ./gradlew :app:assembleDebug
```

The Gradle project calls `externalNativeBuild { cmake { path '../../CMakeLists.txt' } }` so the same root CMakeLists drives both desktop and Android. An `if (CMAKE_SYSTEM_NAME STREQUAL "Android")` branch in `CMakeLists.txt` skips VST3 and the Mac/Windows/Linux installer steps.

Min SDK is 29 (forced by JUCE 8's `juce_Fonts_android.cpp` calling `AFontMatcher` which requires API 29). Target SDK is 36.

### Android-only C++ surface

- [system/include/system/touch_waveform.hpp](system/include/system/touch_waveform.hpp) + [system/src/touch_waveform.cpp](system/src/touch_waveform.cpp) — multi-touch waveform view. Handles tap-to-launch and tap-and-drag-to-scrub.
- [system/include/system/main_component_android.hpp](system/include/system/main_component_android.hpp) + [system/src/main_component_android.cpp](system/src/main_component_android.cpp) — top-level Android component. Composes the top button row (8 layer/voice + view toggle + global), `TouchWaveformView`, bottom transport row (REC/PLAY/STOP/ERASE/LOAD/CTRLS/MOD), and the two panel sheets. Runs the same 100 ms timer + file-chooser handshake + snap-on-select + ModeController integration as desktop `MainComponent`.
- [system/include/system/panel_sheet.hpp](system/include/system/panel_sheet.hpp) + [system/src/panel_sheet.cpp](system/src/panel_sheet.cpp) — slide-up overlay containing the touch-friendly controls for one panel (`"main"` or `"modulation"`). Renders sliders as `HorizontalBarSlider`, buttons as `HorizontalToggleButton`, triggers as `HorizontalTriggerButton`, dropdowns as `HorizontalDropdown` — all defined in an anonymous namespace inside `panel_sheet.cpp`.

All four files are guarded by `#if JUCE_ANDROID`. Desktop builds compile them as empty translation units.

[system/include/system/editor.hpp](system/include/system/editor.hpp) uses `PlatformMainComponent` (`MainComponentAndroid` on Android, `MainComponent` on desktop) so the editor doesn't need its own #ifdef branches.

### Android-only behavior differences from desktop
- Locked landscape orientation (manifest `android:screenOrientation="landscape"`).
- The Instruo font isn't bundled into the APK; UI falls back to system sans-serif (igui::initialise_instruo_font handles the fallback automatically).
- The default `gui/assets/voice.wav` isn't bundled either — apps launch with empty layers. User loads via SAF or records.
- Touch contact-area level uses `MouseEvent::pressure` where supported; devices that don't expose it produce a fixed 0.8 level (see `TouchWaveformView::touch_level_from_event`).
- Only one voice may be touch-scrubbed at a time (newest grab wins).
