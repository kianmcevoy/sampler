# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A JUCE-based DSP prototyping framework, currently scaffolded as a polyphonic sampler instrument. Builds as a standalone app and a VST3 plugin. The repo is intended as a starting point for hardware-bound Instruo DSP projects, so it deliberately separates "instrument" (DSP) from "interface" (everything between the hardware/GUI/MIDI and the DSP) — see `README.md`.

## Build & run

CMake-based. The configured build directory is `build/` (in the workspace, not committed). Standard cycle:

```bash
cd build
cmake ..                       # configure (only needed when CMakeLists.txt changes)
cmake --build . -j$(nproc)     # incremental build
```

VS Code: press **F5** — the `build` task in `.vscode/tasks.json` runs `cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j$(nproc)` from `build/`, and the launch config attaches gdb (Linux) or lldb (macOS) to the Standalone app.

Output artefacts:
- Standalone: `build/sampler_artefacts/Debug/Standalone/sampler` (Linux/Windows) or `.../Standalone/sampler.app` (macOS)
- VST3: `build/sampler_artefacts/Debug/VST3/sampler.vst3`

[install.sh](install.sh) copies the VST3 into `~/instruo/plugins/` for hosting.

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

1. **`gui/src/controls.cpp` :: `build_gui_control_scheme(GuiControlBuilder&)`** — declarative control layout. Each entry has a unique identifier (used as XML tag, OSC address, and JUCE parameter ID) plus a display label and geometry.
2. **`ParameterInterface::process`** ([interface/src/parameter_interface.cpp](interface/src/parameter_interface.cpp)) — reads `GuiControlData`, parses the `juce::MidiBuffer`, reads queued OSC, conditions everything into `ParameterData` for the instrument. Also performs file I/O and publishes display data to `GuiOutputData`.
3. **`Instrument::process`** ([instrument/src/instrument.cpp](instrument/src/instrument.cpp)) — pure DSP. Reads `ParameterData` + `SampleBuffer`, runs the 8-voice pool, writes audio to `PolyDspBuffer` and state to `StateData`. Stays MIDI/GUI-agnostic: it just sees `play`/`stop`/`midi_events` as different trigger sources.
4. **`StateInterface::process`** ([interface/src/state_interface.cpp](interface/src/state_interface.cpp)) — copies per-voice `StateData` (active flags, positions, live-param snapshots) into atomic `GuiOutputData` fields for the GUI thread to read.
5. **`MainComponent`** ([system/src/main_component.cpp](system/src/main_component.cpp)) — GUI-thread coordinator. Runs a 100 ms timer that watches `GuiOutputData` flags (file-loaded, voice-active LEDs) and writes `GuiInputData` (selected_voice, global_mode, file path). Also owns the Voice / Auto / Global mode state machine driven by the voice buttons.

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
- `midi_events` array — per-block note-on/note-off events with pre-computed velocity-scaled level and pitch-scaled speed ratio.

## MIDI subsystem

MIDI parsing, the note stack, and all CC routing live inside `ParameterInterface`. The Instrument never sees `juce::MidiBuffer` — it consumes a fixed-size queue of `MidiNoteEvent` structs.

### Parsing
`isl/include/imidi/` (header-only) provides `imidi::MessageQueueStatic<N>`. Each block, raw bytes from every `juce::MidiMessageMetadata` are pushed in via `from_bytes`, then drained as typed `imidi::Message` events (`NoteOn`, `NoteOff`, `ControlChange`, …).

### Note-on / note-off
- **Note-on**: pick a slot in `active_notes_` (first inactive, or slot 0 if stealing enabled), assign a monotonic `midi_seq_counter_`, compute:
  - `level = slider_level × (velocity / 127)²`  — squared (quasi-perceptual) velocity curve, scaled by the Level slider.
  - `speed = slider_speed × 2^((note − 60) / 12)`  — middle C plays at slider speed, octave up at ×2, octave down at ×0.5. Sign is preserved: a negative speed slider plays in reverse with the same octave relations.
  - Emit `MidiNoteEvent { note_on=true, midi_seq, velocity, speed_ratio }`.
- **Note-on velocity 0** is treated as note-off (running-status convention).
- **Note-off**: find the `active_notes_` slot matching the note number, emit `MidiNoteEvent { note_on=false, midi_seq }`, clear the slot.

### Voice routing on the Instrument side
The Instrument tracks per-voice MIDI ownership in `voice_midi_seq_[slot]`. On each note-on event it allocates a voice via the same `VoiceAllocator` the play button uses (respecting `voice_stealing`), triggers it **gated** so the envelope holds at peak, and records `voice_midi_seq_[slot] = ev.midi_seq`. On each note-off it walks the voices to find the matching seq and calls `voice.release()` (which closes the envelope gate). At the top of every block it clears `voice_midi_seq_[s]` for any voice that has become inactive, so a recycled slot can't be matched by a stale note-off.

`ParameterInterface` does **not** predict which slot the allocator will pick — it just hands out `midi_seq` and lets the Instrument route. This is what keeps the Instrument's allocator policy authoritative.

### CC routing
- **CC1 (Modwheel)** — latched into `ParameterInterface::modwheel_position_` and summed with the start slider. Persists across blocks.

Other CCs are ignored. Add them in `ParameterInterface::process_midi`.

### MIDI + play button coexist
MIDI voices and play-button voices share the voice pool. The play button still works independently — pressing it allocates a new voice via the allocator (ungated AR envelope). The stop button kills voices regardless of source.

## Envelope (AHR)

`idsp::Envelope` (lives in `instrument/include/instrument/envelope.hpp`, will eventually migrate into `isl/include/idsp/`) is an **AHR** envelope: Attack → Hold → Release.

- `trigger(attack, release, shape, gated)` starts attack from zero (or jumps to peak when `attack == 0`).
- If `gated == false` (default, used by the play button / comparator / envelope_trigger): attack-end falls into Release immediately. This is the traditional AR shape.
- If `gated == true` (used by MIDI note-on): attack-end transitions to a **Hold** stage at value 1.0 that persists indefinitely until `release()` is called.
- `release()` closes the gate. Behaviour depends on current stage:
  - **Attack** → marks `release_pending`; attack completes naturally, then release fires. So a brief MIDI note never produces a clicky cutoff — you always hear the full attack.
  - **Hold** → switches to Release immediately, counting down from 1.0.
  - **Idle / Release** → no-op.
- `get_phase()` returns lifetime [0, 1] across the attack+release window. During Hold it sits at the A↔R boundary (`attack_len / total`) so phase-driven modulation has a well-defined value while held.

`shape ∈ [0, 1]`: 0 = exponential (RC), 0.5 = linear, 1 = logarithmic. Quadratic anchors crossfaded — symmetric around the linear midpoint.

## Voice pool, allocator, stealing

`max_voices = 8`. `VoicePool` is a plain `std::array<Voice, 8>` with `kill_all()` and `kill_oldest()` helpers (oldest = smallest `launch_seq_`).

`VoiceAllocator::acquire(pool, voice_stealing, preferred_slot = -1)`:
- If `preferred_slot ∈ [0, 8)` → return that exact slot (used by Voice mode to force a launch into the selected slot).
- Else if any inactive voice exists → return the first inactive.
- Else if `voice_stealing` → return the active voice with the smallest `launch_seq_`.
- Else → `nullptr` (caller silently drops).

Voices have direction *locked at trigger* (`forward_ = speed >= 0`). Modulation of speed magnitude is allowed; sign is not. The base parameters (start, length, level, pan, env durations, mod depths) are kept live-editable via `set_live_params` once per block. The running envelope keeps its original attack/release counters until the next retrigger.

## Three-mode interaction model — Voice / Global / Auto

Driven by the voice buttons + global / auto buttons. State lives in `MainComponent` (GUI thread) and is published to the audio thread via `gui_input.selected_voice` (atomic int, -1 = no selection) and `gui_input.global_mode` (atomic bool). `ParameterInterface` copies these into `parameter.selected_voice` and `parameter.global_mode` each block.

The Instrument resolves this into a three-state mux at the top of `process()`:

- **Voice mode** (`selected_voice ∈ [0, 8)` and that slot is active)
  - Slider edits overlay onto the selected voice each block via `overlay_live_params()` (no random jitter).
  - `play` forces a launch into the selected slot.
  - `stop` kills only the selected voice.
  - GUI snaps sliders to the selected voice's `voice_params_snapshot` when entered.
- **Global mode** (`global_mode == true`)
  - Slider edits overlay onto **every** active voice each block.
  - `play` retriggers all active voices in unison, each with fresh random jitter.
  - `stop` kills all voices.
- **Auto mode** (default; neither voice selected nor global on)
  - Sliders only affect the *next* launch via `build_effective_live_params()` (random jitter applied).
  - Already-running voices are not touched by slider changes.
  - `stop` kills the oldest active voice (one stop = one kill).

Clicking the same voice button twice exits Voice mode → Auto mode. Clicking Global exits both into Global mode. The three modes are mutually exclusive — invariant enforced by `MainComponent`.

## Comparator — phase-driven internal retriggering

A meta-trigger that fires `play` from inside the engine when a chosen phase source crosses threshold buckets.

- `comp_source` dropdown — `None` / `LoopPhase` / `EnvPhase`. Maps to `ComparatorSource::{None, LoopPhase, EnvPhase}`.
- `comp_threshold` slider — bucket width in [0, 1]. 0 disables.
- Source voice = the selected voice if active, else the newest active voice.
- Each sample, compute `bucket = floor(source_val / threshold)`. Fire when `bucket > comp_prev_bucket_` (monotonic increase).
- When the source phase wraps (1 → 0), bucket drops; no fire on the wrap itself. The next upward step past `+threshold` after the wrap fires again.
- "Fire" = allocate a voice via `VoiceAllocator` (respecting `voice_stealing`), build effective live params with random jitter applied, trigger ungated. So comparator voices have AR envelopes, not AHR.

Example with `threshold = 0.25`: fires at phase 0.25, 0.5, 0.75 (and again after the next 0.25 past the wrap) — ~4 retriggers per cycle.

## Per-voice live parameters

`VoiceLiveParams` is the live-editable subset of `ParameterData` (16 fields: start, length, speed, level, pan, loop + 3 envelope params + 2 envelope flags + 5 envelope mod depths). Each Voice holds one copy; the Instrument pushes the current snapshot into the voice via `Voice::set_live_params()` each block, so slider edits to the live-edit target (Voice mode) or to all voices (Global mode) take effect immediately.

The "phase" modulation depths (`phase_*`) are audio-only — they're in `VoiceLiveParams` but **not** mirrored into `VoiceParamSnapshot` because the GUI doesn't display or snap to them.

At trigger time, the Instrument writes the **effective** post-random params into `voice_live_params_[slot]` via `build_effective_live_params()`. So the slot reflects "what this voice is actually playing" (with jitter baked in), which is what the GUI snaps to when you select the voice.

## Cross-thread protocol

`GuiOutputData` (audio → GUI) and `GuiInputData` (GUI → audio) in [interface/include/interface/gui_data.hpp](interface/include/interface/gui_data.hpp) are the only legitimate channel. The handshake for file loading is the canonical pattern:

1. Audio thread sets `gui.request_file_chooser = true` on Load Sample.
2. GUI thread sees the flag, opens `juce::FileChooser`, writes `gui_input.sample_file_path`, sets `gui_input.file_path_ready = true`, clears the request.
3. Audio thread sees `file_path_ready`, loads synchronously inside `ParameterInterface::process`, publishes waveform via `gui.waveform_left/right`, sets `gui.waveform_ready = true` and `gui.file_loaded = true`.
4. GUI thread sees `file_loaded`, clears the flags.

Bulk data (`waveform_left/right`, `voice_params_snapshot`) is only consumed after a release-acquire fence on an atomic flag.

## Sample buffer / playback model

`SampleBuffer` ([instrument/include/instrument/audio_data.hpp](instrument/include/instrument/audio_data.hpp)) holds:
- `PolyDspBuffer loaded_sample` — dynamic-size, source of truth for the waveform; also what the GUI display reads.
- `std::array<idsp::LagrangeDelay<524288>, 2> sample` — static-size playback buffers with 4th-order Lagrange interpolation, indexed by absolute fractional position via `read_at(float position)`.

`LagrangeDelay` (and `LinearDelay` / `AllpassDelay` / `WindowedDelay`) in `isl/include/idsp/delay.hpp` is fundamentally a delay line but the `read_at` method treats internal storage as a generic interpolated buffer. This is how the sampler reads at fractional indices for arbitrary playback speeds.

When loading, `ParameterInterface` writes the audio into both `loaded_sample` (display) and the `sample[]` LagrangeDelays (playback) — these must stay in sync. `num_samples` is capped at 524288 (LagrangeDelay capacity).

## GUI control layout

Defined in [gui/src/controls.cpp](gui/src/controls.cpp) via the `GuiControlBuilder` API. Controls are placed by explicit (x, y) on a 1600 × 840 canvas; the waveform display sits at (450, 45) with size 700 × 150. Current scheme:

| Group | Identifiers | Type |
| --- | --- | --- |
| Transport | `load_sample`, `play`, `stop`, `loop` | triggers + button |
| Playback | `start`, `length`, `speed`, `level`, `pan` | sliders |
| Per-launch random | `random_start/length/speed/level/pan` | sliders |
| Envelope mod depths | `envelope_start/length/speed/level/pan` | sliders (bipolar [−1, +1]) |
| Phase mod depths | `phase_start/length/speed/level/pan` | sliders (bipolar [−1, +1]) |
| Envelope | `time`, `skew`, `shape`, `loop_envelope`, `envelope_sync`, `envelope_trigger`, `voice_stealing` | sliders + buttons + trigger |
| Comparator | `comp_source` (dropdown), `comp_threshold` | dropdown + slider |
| Voice select | `voice_one` … `voice_eight` | voice_buttons (carry slot index 0..7) |
| Mode | `auto`, `global` | buttons |

### GuiControlBuilder methods

- `set_panel_size(w, h)` / `set_display(x, y, w, h)` — canvas + waveform geometry.
- `add_slider(id, label, x, y, w=120, h=120)` — slider with default [0, 1] range.
- `add_slider(id, label, min, max, default, x, y, w, h)` — slider with explicit display range. The underlying JUCE param is normalized [0, 1]; `ParameterInterface` reads the *scaled* value.
- `add_button(id, label, …)` — latching toggle.
- `add_voice_button(id, label, voice_index, …)` — voice select. LED driven by `voice_active[voice_index]`; click is interpreted by `MainComponent` as a Voice-mode selection (and a second click exits to Auto).
- `add_trigger(id, label, …)` — momentary; reads `true` only on the block immediately after the click.
- `add_dropdown(id, label, options, …)` — choice menu; `ParameterInterface` reads the selected index.

### Slider range configuration

Each `add_slider` also creates min/max text fields in the Settings pane. The control value passed into `ParameterInterface::process` is the *scaled* value (after min/max applied). Min/max changes only commit on **Enter**, not on focus loss — see the README note. Slider raw value is otherwise normalized [0, 1].

### Adding a new control

1. Add the line to `build_gui_control_scheme` in [gui/src/controls.cpp](gui/src/controls.cpp).
2. Read it in `ParameterInterface::process` via `input.controls.sliders.at("id")`, `.buttons.at("id")`, `.triggers.at("id")`, or `.dropdowns.at("id")`.
3. Add a field to `ParameterData` if the instrument needs it; assign in ParameterInterface, consume in `Instrument::process`. For per-voice live editability, also add to `VoiceLiveParams` and (if the GUI should snap to it) `VoiceParamSnapshot`.

## State publication

`StateData` ([instrument/include/instrument/state_data.hpp](instrument/include/instrument/state_data.hpp)) is what the Instrument writes each block:

- `playback_position` — float, the first active voice's position, or -1.0 if none.
- `voice_active[8]` / `voice_position[8]` / `voice_volume[8]` — per-voice flags and positions.
- `voice_live_params[8]` — per-voice effective live-param snapshot (post-random for fresh launches; live-edited for selected/global voices).

`StateInterface::process` copies these into the atomic mirrors in `GuiOutputData`: `voice_active`, `voice_position`, `voice_volume`, and the field-by-field `voice_params_snapshot[]`. The GUI consumes the snapshots when the user selects a voice — that's how the sliders snap to the voice's currently-playing values (including its random offset).

## OSC

`ParameterInterface::process` also receives `OscInputData` with queued OSC messages keyed by control identifier (same string as the slider/button). Useful for external control without going through the GUI. Add new routings inside `parameter_interface.cpp`.

## Things that have bitten us

- **JUCE 8 animation module is required.** `igui::elements` uses `juce::Animator` / `juce::VBlankAnimatorUpdater`, which live in the separate `juce_animation` module — already wired into `CMakeLists.txt`'s `target_link_libraries`.
- **`isl/include/idsp/delay.hpp` historically didn't include `idsp/buffer_types.hpp`** despite using `idsp::SampleBufferStatic<N>`. If you see a "does not name a template type" error for `SampleBufferStatic`, check the include order in `instrument/include/instrument/audio_data.hpp` (it includes `buffer_types.hpp` first as a guard).
- **`system/` is no longer a submodule** — it was absorbed into this repo (commit `fe88e4f`) because the project-specific tweaks (MIDI buffer plumbing in `processBlock`, voice-button rendering, grain → playhead rename, panels.cpp waveform-display additions) had diverged too far from the shared template. Edit its files like any other source. The pre-absorption history lives in `JUCE-Prototyper-system` on the `sampler` branch if you ever need to compare.
- **Font lifetime**: `igui::initialise_instruo_font(...)` returns a `unique_ptr<FontLifetimeManager>` that must be stored in `MainComponent` *before* any panel construction and destroyed *after* them. Field declaration order in `main_component.hpp` matters.
- **Audio-thread file I/O**: `ParameterInterface` loads sample files synchronously inside `process()`. This is acceptable for prototyping but blocks the audio thread for the duration of the file read — fine on desktop, won't fly on hardware. To be moved to a worker thread eventually.
- **MIDI velocity 0 is note-off**: don't forget the running-status convention when adding new MIDI handlers — JUCE delivers velocity-0 NoteOn messages literally, and `ParameterInterface::process_midi` routes them through `emit_note_off`. If you add code that reads NoteOn velocity directly, replicate that check.
