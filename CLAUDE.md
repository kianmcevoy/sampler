# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A JUCE-based DSP prototyping framework, currently scaffolded as a sampler instrument. Builds as a standalone app and a VST3 plugin. The repo is intended as a starting point for hardware-bound Instruo DSP projects, so it deliberately separates "instrument" (DSP) from "interface" (everything between the hardware/GUI and the DSP) — see `README.md`.

## Build & run

The project uses CMake. The configured build directory is `build/` and is checked into the workspace (but not committed). Standard cycle:

```bash
cd build
cmake ..                       # configure (only needed when CMakeLists.txt changes)
cmake --build . -j$(nproc)     # incremental build
```

VS Code: press **F5** — the `build` task in `.vscode/tasks.json` runs `cmake -DCMAKE_BUILD_TYPE=Debug .. && make -j$(nproc)` from `build/`, and the launch config attaches gdb (Linux) or lldb (macOS) to the Standalone app.

Output artefacts:
- Standalone: `build/sampler_artefacts/Debug/Standalone/sampler` (Linux/Windows) or `.../Standalone/sampler.app` (macOS)
- VST3: `build/sampler_artefacts/Debug/VST3/sampler.vst3`

No test suite is wired into this project.

## Repository layout — what's a submodule vs. what's yours

```
sampler/
├── instrument/         your DSP code — edit freely
├── interface/          your control-glue code — edit freely
├── gui/                your control-scheme declaration & assets — edit freely
├── system/             SUBMODULE (JUCE-Prototyper-system) — JUCE wrapper, panels, asset manager
│   └── JUCE/           nested submodule, JUCE 8
└── isl/                SUBMODULE — Instruo support library: idsp/ (DSP primitives) + igui/ (UI elements)
```

`system/` is on a `sampler` branch. The old `idsp` and `system/igui` submodules were merged into `isl/`. **Do not** assume `system/igui/` or top-level `idsp/` exist — those are remnants.

`CLAUDE.md` of any of the submodules (if added later) will be loaded automatically by Claude Code — keep instrument-specific guidance here in the project root.

## High-level architecture

There are four classes you actually touch, and they're glued together by the system framework. Data flows in a clear cycle each audio block:

```
GUI controls ──► ParameterInterface ──► Instrument ──► StateInterface ──► GUI display
   (sliders/buttons)   (ParameterData)    (DSP +         (StateData →       (waveform,
                                          StateData)    GuiOutputData)      cursor, etc.)
```

1. **`gui/src/controls.cpp` :: `build_gui_control_scheme(GuiControlBuilder&)`** — declarative list of GUI controls. Each entry has a unique identifier string (used as XML tag / OSC address) and a display label. Controls render on a grid in declaration order.
2. **`ParameterInterface::process`** ([interface/src/parameter_interface.cpp](interface/src/parameter_interface.cpp)) — reads `GuiControlData` by identifier (e.g. `input.controls.sliders.at("start")`), conditions it, and writes `ParameterData` for the instrument. Also performs file I/O (sample loading) and publishes display-only data to `GuiOutputData`.
3. **`Instrument::process`** ([instrument/src/instrument.cpp](instrument/src/instrument.cpp)) — pure DSP. Reads `ParameterData` + `SampleBuffer`, writes audio to `PolyDspBuffer` and state to `StateData`. Should remain hardware-agnostic; the comment on `StateData` calls this out explicitly.
4. **`StateInterface::process`** ([interface/src/state_interface.cpp](interface/src/state_interface.cpp)) — bridges DSP state to `GuiOutputData` for display (currently just truncates the float playback position to int for the cursor).

**The audio thread runs all four.** The GUI thread (JUCE message thread) only reads from `GuiOutputData` and writes to `GuiInputData`. There is no locking — communication uses `std::atomic` for primitives and the "publish via atomic flag" idiom for bulk data (waveform vectors).

## Cross-thread protocol — flags in `interface/include/interface/gui_data.hpp`

`GuiOutputData` (audio → GUI) and `GuiInputData` (GUI → audio) are the *only* legitimate channel between the two threads. The handshake for file loading is the canonical pattern:

1. Audio thread (ParameterInterface) sets `gui.request_file_chooser = true` when the user clicks Load Sample.
2. GUI thread (MainComponent's 100ms timer) sees the flag, opens `juce::FileChooser`, writes the path into `gui_input.sample_file_path`, sets `gui_input.file_path_ready = true`, and clears the request flag.
3. Audio thread sees `file_path_ready`, loads the file (synchronously, inside `ParameterInterface::process`), publishes waveform via `gui.waveform_left/right`, sets `gui.waveform_ready = true` and `gui.file_loaded = true`.
4. GUI thread sees `file_loaded`, clears both flags.

Bulk data (`waveform_left/right`) is only read by the GUI after `waveform_ready.store(true)` — the atomic is the release fence.

## Sample buffer / playback model

`SampleBuffer` (in [instrument/include/instrument/audio_data.hpp](instrument/include/instrument/audio_data.hpp)) holds:
- `PolyDspBuffer loaded_sample` — dynamic-size, source of truth for the loaded waveform; also what the GUI display reads from.
- `std::array<idsp::LagrangeDelay<524288>, 2> sample` — static-size playback buffers with 4th-order Lagrange interpolation. Used for fractional-position reads via `read_at(float position)`.

`LagrangeDelay` is fundamentally a delay line, but `isl/include/idsp/delay.hpp` exposes a `read_at(float position)` method (also on `LinearDelay`, `AllpassDelay`, `WindowedDelay`) that treats the internal storage as a generic audio buffer indexed by absolute position. This is how the sampler reads samples at fractional indices for arbitrary playback speeds.

When loading a sample, `ParameterInterface` writes the audio into both `loaded_sample` (for display) and the `sample[]` LagrangeDelays (for playback) — these must stay in sync. `num_samples` is capped to 524288 (the LagrangeDelay capacity).

## Slider range configuration

Each `add_slider` call also creates a min/max text field pair in the Settings pane. The control value passed into `ParameterInterface::process` is the scaled value (after min/max range applied). Min/max changes only commit on Enter, not on focus loss — see the README note. Slider raw value is otherwise [0, 1].

## Adding a new control

1. Add the line to `build_gui_control_scheme` in [gui/src/controls.cpp](gui/src/controls.cpp).
2. Read it in `ParameterInterface::process` via `input.controls.sliders.at("id")`, `.buttons.at("id")`, `.triggers.at("id")`, or `.dropdowns.at("id")`.
3. Add a field to `ParameterData` if the instrument needs it; assign in ParameterInterface, consume in `Instrument::process`.

## OSC

`ParameterInterface::process` also receives `OscInputData` containing queued OSC messages keyed by control identifier. Same identifier as the slider/button. Useful for external control without touching the GUI.

## Things that have bitten us

- **JUCE 8 animation module is required.** `igui::elements` uses `juce::Animator` / `juce::VBlankAnimatorUpdater`, which live in the separate `juce_animation` module — already wired into `CMakeLists.txt`'s `target_link_libraries`.
- **`isl/include/idsp/delay.hpp` historically didn't include `idsp/buffer_types.hpp`** despite using `idsp::SampleBufferStatic<N>`. If you ever see a "does not name a template type" error for `SampleBufferStatic`, check the include order in `instrument/include/instrument/audio_data.hpp` (it includes `buffer_types.hpp` first as a guard).
- **The `system/` submodule has its own `sampler` branch** with project-specific tweaks (grain → playhead rename, panels.cpp waveform display additions). When updating the submodule, check the branch is right.
- **Font lifetime**: `igui::initialise_instruo_font(...)` returns a `unique_ptr<FontLifetimeManager>` that must be stored in `MainComponent` *before* any panel construction and destroyed *after* them. Field declaration order in `main_component.hpp` matters.
