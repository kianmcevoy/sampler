# Sampler

A polyphonic, layered, granular sampler. Built on JUCE 8 as a Standalone app and a VST3 plugin, with the intention that the DSP core (`instrument/`) can later be lifted onto Instruo hardware unchanged.

This README is the conceptual companion to the code: what each piece *is*, why it exists, and how everything joins up. It focuses on [interface/](interface/) and [instrument/](instrument/) — the two layers you actually edit when designing the sampler's behaviour.

For maintenance notes specifically aimed at coding agents, see [CLAUDE.md](CLAUDE.md).

---

## 1. The shape of the project

The project follows the **Instruo Project Structure** (see the IPS guide). Code is split into layers by responsibility, and data flows through them in a fixed order each audio block:

```
GUI controls ──┐
MIDI buffer ───┼─► ParameterInterface ──► Instrument ──► StateInterface ──► GUI display
OSC input ─────┘
```

| Directory | Role |
| --- | --- |
| `gui/` | Declarative control layout — what sliders, buttons, dropdowns exist and where they live on screen. No logic. |
| `interface/` | Glues the GUI / MIDI / OSC inputs to the DSP. Conditions raw controls into a single `ParameterData` snapshot, parses MIDI, drives the GUI display. **Hardware-aware.** |
| `instrument/` | The DSP. Sees a conditioned `ParameterData` and a set of sample buffers — never a slider, never a MIDI byte. **Hardware-agnostic.** |
| `system/` | JUCE wrapper, audio engine, panel rendering, asset/font management, MIDI plumbing. |
| `isl/` | Instruo support library — DSP primitives (`idsp/`), UI library (`igui/`), MIDI parser (`imidi/`). Submodule. |

The reason for the rigid `instrument/` ↔ `interface/` split is portability: the instrument layer can be lifted onto a hardware platform with the same `ParameterData` contract and *no code changes*. Today the hardware happens to be a laptop running JUCE; tomorrow it might be a Eurorack module with knobs. The instrument doesn't care.

---

## 2. The data contract

Everything between layers is a plain struct. There are no events, no signals, no observers — each layer just reads its input struct and writes its output struct.

### `ParameterData` ([instrument/include/instrument/parameter_data.hpp](instrument/include/instrument/parameter_data.hpp))

Output of `ParameterInterface`, input to `Instrument`. The single conditioned snapshot the DSP consumes each block. Contains everything: slider values, MIDI events, mode flags, marker context, the per-voice live-param array.

The instrument never asks "where did this come from?". `play=true` could mean a button click, an OSC message, or a comparator. `speed=2.0` could mean the slider, MIDI pitch-tracking, or an external CV. The interface's job is to collapse all those sources into one number; the instrument's job is to make audio from it.

### `StateData` ([instrument/include/instrument/state_data.hpp](instrument/include/instrument/state_data.hpp))

Output of `Instrument`. Per-voice playback positions, levels, layer indices, plus per-layer aggregates and per-voice live-param snapshots. Consumed by `StateInterface` and republished as atomics for the GUI thread.

### `SampleBuffer` ([instrument/include/instrument/audio_data.hpp](instrument/include/instrument/audio_data.hpp))

One per layer (8 layers total). Holds:
- `loaded_sample` — dynamic-size `PolyDspBuffer`, the canonical waveform (used for GUI display + onset detection).
- `sample[2]` — `idsp::LagrangeDelay<524288>` × stereo, the playback buffers Voice reads from via `read_at(fractional_index)`.
- `transient_indices[64]` / `transient_count` — cached onset positions for marker mode.

The two storage forms must stay in lockstep — they're both written during `load_sample_into_buffer`.

### `VoiceLiveParams` ([instrument/include/instrument/parameter_data.hpp](instrument/include/instrument/parameter_data.hpp))

The subset of params that are *live-editable per voice*: start, length, speed, level, pan, ADSR, modulation depths, granular deviations. Each voice holds one of these; the instrument pushes the current values in once per block.

The per-launch `random_*` controls are NOT in here — they apply once, at trigger time, then get baked into the voice. Same for the markers / note-route / current_layer state — they're meta, not per-voice.

---

## 3. The control-source abstraction (interface/)

[ParameterInterface](interface/include/interface/parameter_interface.hpp) is where every control source converges into `ParameterData`. The class has three input channels:

| Channel | Type | Read via |
| --- | --- | --- |
| GUI | `GuiControlData` | `input.controls.sliders.at("id")`, etc. |
| MIDI | `juce::MidiBuffer` | parsed by `imidi::MessageQueueStatic<256>` |
| OSC | `OscInputData` | a queue of `(id, value)` messages |

Each block, `ParameterInterface::process`:

1. **Parses MIDI** into typed `imidi::Message` events. Note-ons go through the note-stack (`active_notes_`), get assigned a monotonic `midi_seq`, and produce a `MidiNoteEvent` carrying a velocity factor and pitch ratio. Note-offs look up their slot by note number. CC1 latches the modwheel.
2. **Reads sliders / buttons / triggers / dropdowns** straight into `ParameterData`. No scaling — `gui/src/controls.cpp` declares each slider's range, so the interface gets the value in its target units.
3. **Applies modwheel** by summing it into `start`, clamped to [0, 1].
4. **Applies marker snapping** when enabled. The `start` slider becomes a marker index; the `length` slider becomes a marker count. The conditioned `start` / `length` fractions go into `ParameterData`, and the marker array goes into `marker_fractions[]` so the instrument can quantise its per-launch random jitter onto markers.
5. **Handles the file-chooser handshake** with the GUI thread (see §8).
6. **Handles layer switches** by republishing the new layer's waveform to the GUI.

### Why MIDI lives here, not in the instrument

The instrument layer is hardware-agnostic. Tomorrow's hardware might not have MIDI at all, or might have an entirely different protocol. So MIDI parsing, the note-stack, the velocity-curve, the CC routing all live in the *interface*. The instrument just sees a buffer of normalised `MidiNoteEvent` structs:

```c++
struct MidiNoteEvent {
    bool     note_on;     // false = note-off (release)
    uint64_t midi_seq;    // matches the note-on's seq
    float    velocity;    // already squared, in [0, 1]
    float    note_ratio;  // already 2^((note-60)/12)
    int      note_number; // raw, for Position routing
};
```

This is the shape the instrument is willing to deal with. Want to drive notes from CV pitch + a gate? Build `MidiNoteEvent`s in the interface from whatever the hardware gives you — the instrument doesn't know the difference.

### Note routing

The `note_route` dropdown picks between:
- **Pitch** — `set_midi_offsets(log2(note_ratio), velocity_factor)` on the voice; the voice combines the slider speed/pitch with these offsets.
- **Position** — the note number picks a start fraction (a marker index when markers are on, else `note / 127`). Pitch offset is suppressed.

---

## 4. The DSP layer (instrument/)

[Instrument](instrument/include/instrument/instrument.hpp) is the audio thread's hot path. Each block it:

1. **Reconciles MIDI ownership** — clears `voice_midi_seq_[s]` for any voice that's gone inactive since last block, so recycled slots can't be matched by stale note-offs.
2. **Applies mode mux** (Voice / Global — §5) for live edits and `stop`.
3. **Allocates voices** for `play`, `envelope_trigger`, and per-MIDI-event `note_on`s via `VoiceAllocator`.
4. **Handles latch / scrub / stop_all**.
5. **Pushes live params** into each active voice (with per-voice scaling pickup in Global mode — §5).
6. **Sums audio** voice-major into the output buffer.
7. **Publishes state** to `StateData`.

### Voice pool

Eight `Voice` instances live in [VoicePool](instrument/include/instrument/voice_pool.hpp), a plain `std::array<Voice, 8>`. Allocation policy is in [VoiceAllocator::acquire](instrument/src/voice_pool.cpp):

| Caller asks for | Acquire returns |
| --- | --- |
| `preferred_slot ∈ [0, 8)` | that slot (steals it if active — Voice mode forces this) |
| Any free slot | first inactive |
| All busy, `voice_stealing = true` | active voice with smallest `launch_seq_` (oldest) |
| All busy, `voice_stealing = false` | `nullptr` — caller drops silently |

`launch_seq_` is a monotonic counter incremented per allocation, so "oldest" is unambiguous even after recycling.

### Voice anatomy

[Voice](instrument/include/instrument/voice.hpp) owns:
- A playhead `position_` (float, sample index in its layer's buffer).
- A direction flag `forward_` (locked at trigger time; modulation can't flip it).
- An `idsp::Envelope` (ADSR / AR / none, picked by trigger function).
- A cluster of up to 8 `Grain`s for the granular engine.
- Cached "auto" granular values (`n_eff_samples_`, `kaiser_beta_`, `overlap_eff_`) recomputed each block from the live params.
- MIDI per-note offsets (`midi_octave_offset_`, `midi_velocity_factor_`) — kept *outside* `VoiceLiveParams` so they can't compound on snap-on-select.

Three entry points fix the envelope contract at launch:

| Function | Used by | Envelope |
| --- | --- | --- |
| `trigger_plain` | `play` button | none (level = base, loops forever) |
| `trigger_ar` | `envelope_trigger` button | Attack → Release (one-shot) |
| `trigger_adsr_gated` | MIDI note-on | Attack → Decay → Sustain (held until release) |

The launch path is `prepare_for_trigger → set_live_params → retrigger_position → reset_grains → spawn initial grain → mark active`.

### Live parameter editing

The challenge: when the user sweeps the speed slider, every active voice should respond *now*, not at the next note. But each voice was launched with some randomness baked in, and we'd like to preserve that texture.

Solution: at trigger time, `build_effective_live_params(p, rng)` writes the post-random snapshot into `voice_live_params_[slot]`. Every block, the instrument pushes that snapshot into the voice via `set_live_params`. Voice-mode edits overwrite the slot directly. Global-mode edits use **scaling pickup** so each voice tracks the slider relative to where the slider was when the voice launched (see §5).

### Envelope (idsp::Envelope)

A classic stage-based envelope ([envelope.hpp](instrument/include/instrument/envelope.hpp)):
- `trigger_ar(A, R)` — Attack rises linearly to 1, falls into a quadratic-tail Release.
- `trigger_adsr(A, D, S, R)` — Attack → linear Decay to Sustain level → held → Release on `release()`.
- `release()` always ramps from the *current* value (no jump to peak first if you release during Attack).
- `get_phase()` returns lifetime [0, 1] across A+D+R, with Sustain pinned at the D→R boundary. This drives "envelope phase" as a modulation source.

Curves are linear A, linear D, quadratic R. A/D/R sliders map to [0, 5] s.

### Granular engine

The granular engine ([voice.cpp](instrument/src/voice.cpp)) has two modes selected by the `timestretch` toggle:

**Timestretch ON — C-OLA cluster.** Pitch and time are independent:
- Position advances at `speed` per output sample (loop-traversal rate, signed).
- Each grain reads at `pitch_ratio = 2^(pitch_deviation + midi_octave_offset)` (pitch shift only).
- A new grain spawns every `n_eff_samples_ / overlap_eff_` *output* samples (constant on the output side, so cluster geometry is stable when speed changes).
- The auto-computed sizes / counts are derived per-block in `set_live_params`:
  - `n_auto_s = 40 ms + 30 ms × |pitch_octaves| + 20 ms × max(0, 1 − |speed|)` — slow / pitched playback uses longer grains.
  - `size_deviation` log-scales N_auto (`exp2(size_deviation × 2)`); clamped to [20, 200] ms.
  - `shape_deviation` picks a Kaiser-β LUT (β ∈ {0, 3, 6, 10, 14}); β=6 is roughly Hann.
  - `cola_overlap_for_beta` picks the minimum overlap count for clean reconstruction at the chosen β (1 / 2 / 3 / 4 grains).
  - `grains_deviation` bipolar-tweaks the overlap toward 1 (sparse, choppy) or 8 (lush, smeared).

Per-grain randomness comes from the `random_*` sliders: pitch, size, shape, position, level, pan, grain-hop. The jitter curve is `depth × structured + depth^4 × spray`, with `depth=0` deterministic, `depth=0.5` decorrelating, `depth=1` chaotic.

**Timestretch OFF — loop-crossfade.** Pitch tracks speed exactly (single playhead):
- One Body grain reads at `speed`.
- Within half a grain length of the loop boundary, the Body becomes a FadeOut and a FadeIn spawns at the start of the loop region. The crossfade is a linear → raised-cosine blend governed by `shape_deviation`.
- When the FadeOut finishes, the FadeIn promotes to Body and the cycle repeats.

This is what gives you traditional looped sampler playback (no granular smearing) when you want it.

### Modulation depths

Each of the 5 playback params (`start`, `length`, `speed`, `level`, `pan`) has two modulation sources, both bipolar in [-1, +1]:

- **envelope_*** — multiplied by the envelope's current value `e`.
- **phase_*** — multiplied by the voice's lifetime phase `ph`.

For `level` and `phase_level`, the depth acts as wet/dry between "ignore the modulator" (depth=0 → factor=1) and "modulator is the VCA" (depth=±1 → factor=`e` or `1-e`).

For `speed`, modulation can scale the magnitude but cannot flip the sign (clamped at 0 before reapplying `forward_`). Slider edits *can* flip the sign — they're treated as a deliberate retrigger of direction.

---

## 5. The two-mode interaction model — Voice / Global

The eight voice buttons + the Global button form a radio. Mode state lives in [ModeController](interface/src/mode_controller.cpp) (a GUI-thread helper inside `interface/`), published to the audio thread as two atomics: `selected_voice` and `global_mode`.

### Voice mode (a voice button is held selected)

The selected slot becomes the live-edit target:
- Slider edits overlay directly onto the voice (`overlay_live_params`).
- `play` forces a launch into the selected slot.
- `stop` kills only that voice.
- The GUI snaps its sliders to the voice's effective values when you enter (and restores the pre-entry slider state when you leave).

### Global mode (no voice selected — the default)

Slider edits affect every voice on the current layer, but with **value-scaling pickup** for the 5 playback params. At trigger time we capture both the voice's effective value and the slider position. While the slider is at its anchor, the voice plays its trigger-time value. As the slider moves away from the anchor, the voice's value rescales piecewise-linearly toward the slider's min/max:

```
                  voice value
                       ▲
                  max──┤        ◇  <- slider at max collapses every voice onto slider value
                       │       /
                       │      /
        trig value ────┼─────●  <- slider at anchor, voice at its post-random value
                       │   /│
                       │ /  │
                  min──◇    │  <- slider at min collapses every voice onto slider value
                       ◇    │
                       └─┬──┬──► slider value
                         anchor (= where the slider was at trigger time)
```

This lets you sweep all eight voices simultaneously without losing their random texture: returning the slider to its anchor restores every voice's original value.

For the non-playback live fields (envelope, phase, granular deviations, ADSR), Global mode just direct-overlays — no scaling pickup. These are more like global "voicing" parameters where everyone agreeing on the same value is what you want.

`play` in Global mode allocates a new voice (doesn't retrigger existing ones); `stop` kills every voice on the editing layer. `stop_all` is the cross-layer kill switch.

### How the modes interact with layers

The current_layer scopes Global-mode operations: edits affect only voices on that layer, `stop` kills only voices on that layer, `play` tags the new voice with that layer. Voices on other layers keep playing what they were last commanded to play (stored in `voice_effective_params_`), so wandering to a different layer to edit doesn't disturb them.

---

## 6. Layers — 8 independent sample slots

Each of the 8 layers owns a `SampleBuffer` (its own sample, its own onset cache). Voices are *not* per-layer — there are still only 8 voices, but each voice records the layer it was triggered on (`set_layer(current_layer)`) and reads from that layer's buffer for its lifetime.

```
8 voices  ──pool──►  Voice 0 ──(layer=2)──► layer_buffers[2].sample[]
                     Voice 1 ──(layer=0)──► layer_buffers[0].sample[]
                     Voice 2 ──(layer=2)──► layer_buffers[2].sample[]
                     ...                                ▲
                                          loaded by ParameterInterface
                                          when current_layer=2 + Load Sample
```

GUI-side, the `layer_view` button toggles the meaning of the voice buttons:
- **Voice view** (default) — voice buttons select/deselect voices, with per-voice LEDs.
- **Layer view** — voice buttons become layer selectors, with per-layer LEDs (`layer_summed_envelope[]` drives the brightness).

Switching to layer view force-deselects any selected voice (`ModeController::deselect_voice`).

---

## 7. Bidirectional position scrubbing

The `position` slider is unusual: the audio thread *also* writes to it. The GUI's 100 ms timer reads `playback_position_normalized` (the loop-position fraction of the routing-target voice) and pushes it back as the slider's value, so the slider follows the active voice as it plays.

To tell apart "audio is echoing back" from "user is dragging", the GUI sets `position_scrubbing = true` while the slider is being gestured (`parameter_being_gestured()`). The audio thread only acts on the slider value when this flag is true:

- **Voice mode** → jump-to on the selected voice.
- **Global mode** → on the rising edge of `position_scrubbing`, snapshot the slider value + each in-layer voice's current loop position; while the drag continues, rescale each voice's position through its own anchor.
- **Otherwise** → jump-to on the newest active voice on the current layer.

This is what makes the position slider a usable scrub control across all eight voices without teleporting them all to the same point.

---

## 8. The cross-thread protocol

The audio thread and GUI thread never block on each other. Communication is via two structs full of atomics in [interface/include/interface/gui_data.hpp](interface/include/interface/gui_data.hpp):

- **`GuiOutputData`** — audio → GUI (the audio thread *writes*, the GUI thread *reads*).
- **`GuiInputData`** — GUI → audio (the GUI thread *writes*, the audio thread *reads*).

Primitive fields are `std::atomic<T>`. Bulk data (waveform vectors, per-voice param snapshots) follows the "publish via atomic flag" idiom: the writer fills the buffer, then sets a `ready` flag with release semantics; the reader checks the flag with acquire semantics, then reads the buffer.

### File loading handshake

The canonical example of cross-thread coordination:

1. User clicks **Load Sample** → audio thread sets `gui.request_file_chooser = true`.
2. GUI thread sees the flag → opens `juce::FileChooser` (which is GUI-thread-only) → writes the path into `gui_input.sample_file_path` → sets `gui_input.file_path_ready = true` → clears the request.
3. Audio thread sees `file_path_ready` → loads the file synchronously inside `ParameterInterface::process` (into the currently-selected layer) → publishes the waveform → sets `gui.file_loaded = true` and `gui.waveform_ready = true`.
4. GUI thread sees `file_loaded` → clears the flags → repaints the waveform display.

Loading runs on the audio thread today (acceptable for prototyping) but will eventually move to a worker thread for hardware.

### Snap-on-select

When the user selects a voice, the GUI needs to snap its sliders to that voice's *currently playing* values — not the trigger-time values, since the user may have edited the voice in Voice mode, or scaled it in Global mode. The audio thread publishes per-voice `voice_live_params[8]` into `StateData` every block, and `StateInterface` mirrors them field-by-field into `GuiOutputData::voice_params_snapshot[]` atomic arrays. `ModeController::read_voice_snapshot` reads those atomics back into a `VoiceLiveParams` and the helper apply-fn drops the values into the JUCE parameters.

The mapping between `VoiceLiveParams` fields and JUCE parameter IDs is the [voice_param_table](interface/include/interface/voice_param_table.hpp) — a single `std::array` of `{id, pointer-to-member}` rows. To add a new per-voice editable parameter, add a row to the table; everything else (atomic mirror size, publish loop, snap-on-select) follows automatically.

---

## 9. Markers — quantising start/length to musical positions

The `markers` button enables marker mode. The `marker_type` dropdown picks between:

- **Time** — an evenly-spaced grid of N markers across the buffer.
- **Transient** — onset-detected positions, computed at file-load time and cached in `SampleBuffer::transient_indices`.

When markers are on, `ParameterInterface`:
1. Builds the N markers (grid or transients).
2. Reinterprets the `start` slider as a marker index (slider * N).
3. Reinterprets the `length` slider as a marker count (slider * N + 1, clamped to N − start_marker).
4. Publishes the conditioned `start` / `length` fractions into `ParameterData`, plus the full `marker_fractions[]` array.

The instrument's per-launch random jitter (`build_effective_live_params`) checks `markers_enabled` and, when set, jitters the integer marker index instead of the continuous start value. So at `random_start = 1` a launch picks a random marker; in between, it picks a marker within a window proportional to the random depth.

Onset detection ([instrument/include/instrument/onset.hpp](instrument/include/instrument/onset.hpp)) is a fairly conventional pipeline:

```
mono → HPF(150 Hz) → |x| → fast/slow envelope follower → max(0, Δ envelope)
     → adaptive threshold (mean + 3σ) → 30 ms cluster suppression
     → zero-crossing rewind (~10 ms) → top-64 by strength
```

The output is up to 64 sample indices sorted by time. Running on the audio thread at load is fine for a 10-second sample; for longer files this should be moved off-thread.

---

## 10. State publication — what the GUI reads

`StateData` is written by the Instrument each block:

| Field | Drives |
| --- | --- |
| `playback_position` | Legacy single cursor (first active voice) |
| `playback_position_normalized` | Bidirectional `position` slider |
| `voice_active[8]` | Voice-button LED on/off |
| `voice_position[8]` | Per-voice cursor lines on the waveform |
| `voice_volume[8]` | Per-voice button LED brightness |
| `voice_layer[8]` | Layer-aware rendering |
| `voice_live_params[8]` | Snap-on-select |
| `layer_has_active_voices[8]` | Layer-view button LED on/off |
| `layer_summed_envelope[8]` | Layer-view button LED brightness |

`StateInterface` copies these to atomics in `GuiOutputData` for the GUI thread to consume. Note that the StateInterface is deliberately dumb — almost a pure memcpy. The reason is twofold: (1) anything fancier risks bleeding instrument concerns into UI concerns, and (2) on hardware the StateInterface is where you'd do final-mile preparation for display drivers (LED PWM curves, segment-display formatting, etc.) — so leaving it lightweight here keeps the migration path obvious.

---

## 11. Adding things — the quick recipes

### A new slider

1. Add a line to [gui/src/controls.cpp](gui/src/controls.cpp) inside `build_gui_control_scheme`:
   ```c++
   controls.add_slider("modulation", "wibble", "Wibble", 0.f, 1.f, 0.5f, x, y);
   ```
2. Read it in `ParameterInterface::process`:
   ```c++
   p.wibble = input.controls.sliders.at("wibble");
   ```
3. Add `float wibble;` to `ParameterData`.
4. Consume `p.wibble` in `Instrument::process`.

### A new per-voice live-editable param

After steps 1–3 above:
5. Add `float wibble { 0.f };` to `VoiceLiveParams`.
6. Add a row to `voice_param_floats` in [voice_param_table.hpp](interface/include/interface/voice_param_table.hpp):
   ```c++
   {"wibble", &VoiceLiveParams::wibble},
   ```
7. Mirror it in `build_effective_live_params`, `overlay_live_params`, and `overlay_non_playback_fields` if it's a non-playback field (i.e. shouldn't go through scaling pickup).
8. Consume it in `Voice::set_live_params`.

Steps 1–7 are pure plumbing; step 8 is the only place where DSP changes.

### A new MIDI CC

In `ParameterInterface::process_midi`, in the `ControlChange` branch:

```c++
if (d.controller == 1) {
    this->modwheel_position_ = static_cast<float>(d.value) / 127.f;
} else if (d.controller == 7) {       // CC7 = main volume
    this->cc_volume_ = static_cast<float>(d.value) / 127.f;
}
```

Latch it into a member, then sum / multiply it into the relevant `ParameterData` field in `process`.

### A new sample-trigger source (CV gate, OSC, MIDI clock…)

Anything that *triggers* should look like a `MidiNoteEvent` (or set `play=true`) by the time it reaches `ParameterData`. So:
- For one-off triggers, set `out.play = true` from your handler in `ParameterInterface`.
- For pitched / gated triggers, build a `MidiNoteEvent` and append it to `out.midi_events` (respecting `max_midi_events_per_block`).
- For analogue CV with continuous pitch tracking, you'd usually add a new field to `ParameterData` (`cv_pitch_ratio`) and combine it into the speed inside `set_live_params`.

---

## 12. Things to know when rewriting

A few non-obvious invariants the current code relies on:

- **Direction is locked at trigger.** `forward_ = (speed >= 0)` at trigger time, then modulation can't flip the sign. Slider edits can, because `set_live_params` re-derives `forward_` from the live speed.
- **MIDI offsets live outside `VoiceLiveParams`.** Snap-on-select copies `VoiceLiveParams` from a published snapshot; if the MIDI velocity factor were in there it would compound on every snap. `midi_octave_offset_` / `midi_velocity_factor_` are voice-private and applied during DSP, not snapshotted.
- **Off-layer voices in Global mode use `voice_effective_params_`, not `voice_live_params_`.** The first is "what we last actually pushed into the voice", the second is "the trigger-time anchor for scaling pickup". If you only had one array, off-layer voices would snap back to their trigger value the moment the user wandered to another layer.
- **Grain spawn rate is in *output* samples, not source samples.** This is why pitch-shifting the cluster doesn't double the grain count. `synth_hop_counter_ -= 1.f` per output sample.
- **`play=true` is a one-block trigger** (the GUI's `add_trigger` produces a `true` only on the block after the click). Don't latch it or you'll re-fire forever.
- **`stop_all` is the only cross-layer kill** — every other stop / overlay operation is scoped to `current_layer`.
- **The audio thread loads files synchronously.** This is wrong for hardware but correct enough for the desktop prototype. If you change this, you'll need a worker thread + a triple-buffer for the LagrangeDelay swap.
- **`file(GLOB ...)` in `CMakeLists.txt`** caches the source list at configure time. Adding or removing a `.cpp` requires `cmake ..` before the next build.

---

## 13. Quick reference — file map

```
instrument/
├── include/instrument/
│   ├── audio_data.hpp        SampleBuffer (PolyDspBuffer + LagrangeDelay + transients)
│   ├── constants.hpp         max_voices, max_layers (both 8)
│   ├── envelope.hpp          idsp::Envelope (ADSR / AR)
│   ├── instrument.hpp        Instrument (the audio-thread process function)
│   ├── onset.hpp             idsp::detect_onsets (transient marker detection)
│   ├── parameter_data.hpp    ParameterData, VoiceLiveParams, MidiNoteEvent, VoiceSliderAnchor
│   ├── state_data.hpp        StateData (instrument → state interface)
│   ├── voice.hpp             Voice (the granular cluster + envelope per slot)
│   └── voice_pool.hpp        VoicePool, VoiceAllocator
└── src/
    ├── instrument.cpp        Top-level process: mode mux, triggers, scrub, sum
    ├── voice.cpp             Voice::set_live_params + Voice::process (granular + crossfade)
    └── voice_pool.cpp        VoicePool::kill_all + allocator policy

interface/
├── include/interface/
│   ├── gui_data.hpp          GuiOutputData / GuiInputData (the audio↔GUI mailbox)
│   ├── mode_controller.hpp   Voice / Global mode machine
│   ├── parameter_interface.hpp
│   ├── state_interface.hpp
│   ├── utility_data.hpp      (empty — placeholder per IPS)
│   └── voice_param_table.hpp Single source of truth for snap-on-select mirroring
└── src/
    ├── mode_controller.cpp
    ├── parameter_interface.cpp  GUI/MIDI/OSC → ParameterData, file loads, marker snap
    └── state_interface.cpp      StateData → atomic GUI mirrors

gui/
└── src/controls.cpp          Declarative GUI control scheme
```

Anything not listed above lives in `system/` and is plumbing — JUCE wiring, panel rendering, the audio engine itself. You shouldn't usually need to touch it.
