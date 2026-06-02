#include "interface/parameter_interface.hpp"
#include "JuceHeader.h"
#include "idsp/functions.hpp"
#include "instrument/onset.hpp"
#include "system/asset_manager.hpp"

#include <cmath>
#include <vector>


ParameterInterface::ParameterInterface(ParameterInterfaceOutputData& output)
{
    // Pre-load the bundled default sample into layer 0 so the sampler is
    // usable without going through the file chooser first. selected_layer
    // defaults to 0, so this is also the layer the GUI displays at launch.
    const auto default_sample = AssetManager::get_resource_file("gui/assets/voice.wav");
    if (load_sample_into_buffer(default_sample, output.layer_buffers[0], output.gui, 0.f, 1.f))
    {
        // Trigger a GUI repaint of the waveform once the panels come up.
        output.gui.file_loaded.store(true);
    }
}

void ParameterInterface::load(const ParameterInterfaceLoadData& /*loaded*/,
                              ParameterInterfaceOutputData& /*output*/)
{
}

void ParameterInterface::process(const ParameterInterfaceInputData& input,
                                 ParameterInterfaceOutputData& output)
{
    auto& p = output.parameter;

    // Parse MIDI first so velocity/pitch reads use the current slider values
    // captured *before* any modwheel-driven start offset is applied below.
    this->process_midi(input.midi, input.controls, p);

    // Sliders return values already in their displayed ranges (configured in
    // gui/src/controls.cpp), so no rescaling is needed here.
    p.play             = input.controls.triggers.at("play");
    p.stop             = input.controls.triggers.at("stop");
    p.stop_all         = input.controls.triggers.at("stop_all");
    p.latch            = input.controls.triggers.at("latch");
    p.timestretch      = input.controls.buttons.at("timestretch");
    p.loop             = input.controls.buttons.at("loop");
    p.position         = input.controls.sliders.at("position");
    // Modwheel sums with the start slider, clamped to [0, 1].
    p.start  = idsp::clamp(input.controls.sliders.at("start") + this->modwheel_position_, 0.f, 1.f);
    p.length = input.controls.sliders.at("length");
    p.speed  = input.controls.sliders.at("speed");
    p.level  = input.controls.sliders.at("level");
    p.pan    = input.controls.sliders.at("pan");

    p.attack  = input.controls.sliders.at("attack");
    p.decay   = input.controls.sliders.at("decay");
    p.sustain = input.controls.sliders.at("sustain");
    p.release = input.controls.sliders.at("release");

    p.filter_freq = input.controls.sliders.at("filter_freq");
    p.filter_q    = input.controls.sliders.at("filter_q");

    p.voice_stealing   = input.controls.buttons.at("voice_stealing");
    p.envelope_trigger = input.controls.triggers.at("envelope_trigger");
    p.scale_envelope   = input.controls.buttons.at("scale_envelope");

    p.random_speed  = input.controls.sliders.at("random_speed");
    p.random_start  = input.controls.sliders.at("random_start");
    p.random_length = input.controls.sliders.at("random_length");
    p.random_level  = input.controls.sliders.at("random_level");
    p.random_pan    = input.controls.sliders.at("random_pan");
    p.random_cutoff = input.controls.sliders.at("random_cutoff");

    p.envelope_speed  = input.controls.sliders.at("envelope_speed");
    p.envelope_start  = input.controls.sliders.at("envelope_start");
    p.envelope_length = input.controls.sliders.at("envelope_length");
    p.envelope_level  = input.controls.sliders.at("envelope_level");
    p.envelope_pan    = input.controls.sliders.at("envelope_pan");
    p.envelope_cutoff = input.controls.sliders.at("envelope_cutoff");
    p.envelope_resonance = input.controls.sliders.at("envelope_resonance");

    p.phase_speed  = input.controls.sliders.at("phase_speed");
    p.phase_start  = input.controls.sliders.at("phase_start");
    p.phase_length = input.controls.sliders.at("phase_length");
    p.phase_level  = input.controls.sliders.at("phase_level");
    p.phase_pan    = input.controls.sliders.at("phase_pan");

    p.pitch_deviation  = input.controls.sliders.at("pitch");
    p.size_deviation   = input.controls.sliders.at("size");
    p.shape_deviation  = input.controls.sliders.at("shape");
    p.grains_deviation = input.controls.sliders.at("grains");

    p.random_pitch    = input.controls.sliders.at("random_pitch");
    p.random_size     = input.controls.sliders.at("random_size");
    p.random_shape    = input.controls.sliders.at("random_shape");
    p.random_grains   = input.controls.sliders.at("random_grains");
    p.random_position = input.controls.sliders.at("random_position");

    // Selected voice (GUI-thread state). The Instrument uses this both to
    // route live edits onto a specific voice and to force a `play` into that
    // slot. -1 means "no selection" — fall back to the normal allocator.
    p.selected_voice     = input.gui.selected_voice.load();
    p.global_mode        = input.gui.global_mode.load();
    p.position_scrubbing = input.gui.position_scrubbing.load();

    // Layer routing. selected_layer chooses which buffer marker/snap reads
    // see, where new triggers land, and which waveform the GUI displays.
    {
        const int sel = idsp::clamp(input.gui.selected_layer.load(),
                                    0, static_cast<int>(max_layers) - 1);
        p.current_layer = sel;
        // Republish waveform on layer-switch so the display swaps even if no
        // new sample was loaded into the newly-selected layer.
        if (sel != last_published_layer_)
        {
            publish_waveform(output.layer_buffers[sel], output.gui,
                             input.controls.sliders.at("start"),
                             input.controls.sliders.at("length"));
            last_published_layer_ = sel;
        }
    }

    // --- markers: read controls, optionally snap start/length, publish to GUI.
    const bool   markers_on = input.controls.buttons.at("markers");
    const size_t mtype      = input.controls.dropdowns.at("marker_type");
    const int    resolution = static_cast<int>(input.controls.sliders.at("resolution"));
    p.markers_enabled = markers_on;
    p.marker_type     = static_cast<int>(mtype);
    p.resolution      = resolution;

    p.note_route_mode = static_cast<int>(input.controls.dropdowns.at("note_route"));

    // Marker / waveform reads always target the currently-selected layer.
    const SampleBuffer& active_buffer = output.layer_buffers[p.current_layer];
    const int buffer_size = static_cast<int>(active_buffer.loaded_sample.channel(0).size());

    if (markers_on && buffer_size > 0)
    {
        // Build the effective marker array (sample indices, sorted ascending).
        std::array<int, 64> markers{};
        int N = 0;
        if (mtype == 0)
        {
            N = idsp::clamp(resolution, 1, 64);
            for (int i = 0; i < N; ++i)
            {
                markers[i] = static_cast<int>(
                    static_cast<float>(i) / static_cast<float>(N) * static_cast<float>(buffer_size));
            }
        }
        else
        {
            N = idsp::min(resolution, active_buffer.transient_count);
            for (int i = 0; i < N; ++i) markers[i] = active_buffer.transient_indices[i];
        }

        if (N > 0)
        {
            // Snap start: slider [0,1] picks a marker index. Modwheel still applies
            // before the snap so MIDI-driven offsets walk through markers too.
            const float start_slider = idsp::clamp(
                input.controls.sliders.at("start") + this->modwheel_position_, 0.f, 1.f);
            const int start_marker = idsp::clamp(
                static_cast<int>(start_slider * static_cast<float>(N)), 0, N - 1);
            const float start_frac =
                static_cast<float>(markers[start_marker]) / static_cast<float>(buffer_size);

            // Snap length: slider [0,1] maps to [1, N - start_marker] markers.
            // End fraction is the next marker after the span, or end-of-buffer
            // when the span runs out.
            const float length_slider = idsp::clamp(input.controls.sliders.at("length"), 0.f, 1.f);
            const int length_markers = idsp::clamp(
                static_cast<int>(length_slider * static_cast<float>(N)) + 1, 1, N - start_marker);
            const int end_marker_idx = start_marker + length_markers;
            const float end_frac = (end_marker_idx < N)
                ? static_cast<float>(markers[end_marker_idx]) / static_cast<float>(buffer_size)
                : 1.f;

            p.start  = start_frac;
            p.length = end_frac - start_frac;

            // Publish marker context so the instrument's per-launch random
            // jitter can quantise onto marker positions instead of producing
            // continuous values that drift off the marker grid.
            p.marker_count   = N;
            p.start_marker   = start_marker;
            p.length_markers = length_markers;
            for (int i = 0; i < N;  ++i)
            {
                p.marker_fractions[i] =
                    static_cast<float>(markers[i]) / static_cast<float>(buffer_size);
            }
            for (int i = N; i < 64; ++i) p.marker_fractions[i] = 0.f;
        }
        else
        {
            p.marker_count = 0;
        }

        for (int i = 0; i < N;  ++i) output.gui.marker_positions[i].store(markers[i]);
        for (int i = N; i < 64; ++i) output.gui.marker_positions[i].store(-1);
        output.gui.marker_count.store(N);
    }
    else
    {
        p.marker_count = 0;
        output.gui.marker_count.store(0);
    }

    // Update the display markers on the waveform view from the (post-modwheel,
    // post-snap) parameter values, so the gold range markers track what's
    // actually playing.
    if (output.gui.waveform_ready.load() && !output.gui.waveform_left.empty())
    {
        const int num_samples = static_cast<int>(output.gui.waveform_left.size());
        if (num_samples > 0)
        {
            const int start_point = static_cast<int>(p.start * num_samples);
            const int duration    = idsp::max(static_cast<int>(p.length * num_samples), 1);
            output.gui.display_marker_start = idsp::clamp(start_point, 0, num_samples - 1);
            output.gui.display_marker_end   = idsp::min(start_point + duration, num_samples);
        }
    }
    else
    {
        output.gui.display_marker_start.store(0);
        output.gui.display_marker_end.store(0);
    }

    // File chooser handshake.
    if (input.controls.triggers.at("load_sample"))
    {
        output.gui.request_file_chooser.store(true);
    }

    if (input.gui.file_path_ready.load())
    {
        const auto& file_path = input.gui.sample_file_path;
        if (!file_path.empty())
        {
            // Load into the currently-selected layer's buffer. Layers other
            // than the selected one are untouched, so existing voices on those
            // layers keep playing their original sample.
            this->load_sample_into_buffer(juce::File(file_path),
                                          output.layer_buffers[p.current_layer],
                                          output.gui,
                                          input.controls.sliders.at("start"),
                                          input.controls.sliders.at("length"));
        }

        // Always acknowledge the handshake — GUI clears file_path_ready on this.
        output.gui.file_loaded.store(true);
    }
}

bool ParameterInterface::load_sample_into_buffer(const juce::File& audio_file,
                                                 SampleBuffer& target, GuiOutputData& gui,
                                                 float start_slider, float length_slider)
{
    if (!audio_file.existsAsFile()) return false;

    juce::AudioFormatManager format_manager;
    format_manager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(format_manager.createReaderFor(audio_file));
    if (reader == nullptr) return false;

    const int num_channels = static_cast<int>(reader->numChannels);
    // LagrangeDelay storage is statically sized; cap num_samples to its capacity.
    constexpr int max_samples = 524288;
    const int num_samples = idsp::min(static_cast<int>(reader->lengthInSamples), max_samples);

    target.loaded_sample.resize(num_samples);
    target.sample[0].reset();
    target.sample[1].reset();

    if (num_channels == 1)
    {
        std::vector<float> temp_buffer(num_samples);
        float* channel_ptr = temp_buffer.data();
        reader->read(&channel_ptr, 1, 0, num_samples);

        for (int i = 0; i < num_samples; ++i)
        {
            target.loaded_sample.channel(0)[i] = temp_buffer[i];
            target.loaded_sample.channel(1)[i] = temp_buffer[i];
            target.sample[0].write(temp_buffer[i]);
            target.sample[1].write(temp_buffer[i]);
        }
    }
    else
    {
        std::vector<float> temp_buffer_l(num_samples);
        std::vector<float> temp_buffer_r(num_samples);
        float* channels[2] = { temp_buffer_l.data(), temp_buffer_r.data() };

        reader->read(channels, 2, 0, num_samples);

        for (int i = 0; i < num_samples; ++i)
        {
            target.loaded_sample.channel(0)[i] = temp_buffer_l[i];
            target.loaded_sample.channel(1)[i] = temp_buffer_r[i];
            target.sample[0].write(temp_buffer_l[i]);
            target.sample[1].write(temp_buffer_r[i]);
        }
    }

    target.loaded_sample.update();

    // Onset detection — runs synchronously on the audio thread, same as the
    // surrounding file I/O. Produces up to 64 transient sample indices for
    // marker mode. Mono mixdown is L+R averaged.
    {
        std::vector<float> mono(static_cast<size_t>(num_samples));
        for (int i = 0; i < num_samples; ++i)
        {
            mono[static_cast<size_t>(i)] = 0.5f * (target.loaded_sample.channel(0)[i]
                                                 + target.loaded_sample.channel(1)[i]);
        }
        const auto onsets = idsp::detect_onsets(
            mono.data(), static_cast<size_t>(num_samples),
            static_cast<float>(reader->sampleRate));
        target.transient_indices  = onsets.indices;
        target.transient_count    = onsets.count;
        target.loaded_sample_rate = static_cast<float>(reader->sampleRate);
    }

    publish_waveform(target, gui, start_slider, length_slider);
    return true;
}

void ParameterInterface::publish_waveform(const SampleBuffer& target, GuiOutputData& gui,
                                          float start_slider, float length_slider)
{
    const int num_samples = static_cast<int>(target.loaded_sample.channel(0).size());
    if (num_samples <= 0)
    {
        // Layer with no sample loaded — clear the display.
        gui.waveform_ready.store(false);
        gui.waveform_left.clear();
        gui.waveform_right.clear();
        gui.display_marker_start.store(0);
        gui.display_marker_end.store(0);
        return;
    }

    const int start_point = static_cast<int>(start_slider * num_samples);
    const int duration    = idsp::max(static_cast<int>(length_slider * num_samples), 1);
    gui.display_marker_start = idsp::clamp(start_point, 0, num_samples - 1);
    gui.display_marker_end   = idsp::min(start_point + duration, num_samples);

    gui.waveform_left.resize(num_samples);
    gui.waveform_right.resize(num_samples);
    for (int i = 0; i < num_samples; ++i)
    {
        gui.waveform_left[i]  = target.loaded_sample.channel(0)[i];
        gui.waveform_right[i] = target.loaded_sample.channel(1)[i];
    }
    gui.waveform_ready.store(true);
}

void ParameterInterface::process_midi(const juce::MidiBuffer& midi,
                                      const GuiControlData& controls,
                                      ParameterData& out)
{
    out.midi_event_count = 0;

    const bool voice_stealing = controls.buttons.at("voice_stealing");

    // Push raw MIDI bytes through the parser. JUCE delivers each message as
    // a contiguous block already framed by status byte; the imidi queue
    // handles running-status and multi-byte streams internally.
    for (const auto meta : midi)
    {
        this->midi_queue_.from_bytes(meta.data, static_cast<size_t>(meta.numBytes));
    }

    while (this->midi_queue_.num_messages() > 0
        && out.midi_event_count < ParameterData::max_midi_events_per_block)
    {
        const imidi::Message msg = this->midi_queue_.read_message();
        const auto t = msg.type();

        if (t == imidi::MessageType::NoteOn)
        {
            const auto d = msg.data_as<imidi::Message::NoteOn>();
            if (d.velocity == 0)
            {
                // MIDI running-status convention: NoteOn velocity 0 == NoteOff.
                this->emit_note_off(d.note, out);
                continue;
            }

            const int slot = this->allocate_midi_voice_slot(voice_stealing);
            if (slot < 0) continue;  // all slots busy, stealing off — drop

            const uint64_t seq = ++this->midi_seq_counter_;
            this->active_notes_[slot] = ActiveNote { /*active=*/true, /*note=*/d.note, /*midi_seq=*/seq };

            const float v01 = static_cast<float>(d.velocity) / 127.f;
            // Perceptual-quadratic velocity factor; the Voice multiplies its
            // per-grain level by this internally (it never appears in any
            // VoiceLiveParams snapshot, so the slider can't compound it).
            const float velocity_factor = v01 * v01;
            // Pure pitch ratio (no slider speed baked in) — the Voice combines
            // it with the live slider values during set_live_params.
            const float ratio = std::pow(2.f, (static_cast<float>(d.note) - 60.f) / 12.f);

            out.midi_events[out.midi_event_count++] = MidiNoteEvent {
                /*note_on=*/true,
                /*midi_seq=*/seq,
                /*velocity=*/velocity_factor,
                /*note_ratio=*/ratio,
                /*note_number=*/static_cast<int>(d.note),
            };
        }
        else if (t == imidi::MessageType::NoteOff)
        {
            const auto d = msg.data_as<imidi::Message::NoteOff>();
            this->emit_note_off(d.note, out);
        }
        else if (t == imidi::MessageType::ControlChange)
        {
            const auto d = msg.data_as<imidi::Message::ControlChange>();
            if (d.controller == 1)
            {
                // CC1 (modwheel) — latched, persists across blocks.
                this->modwheel_position_ = static_cast<float>(d.value) / 127.f;
            }
        }
        // PitchBend / ProgramChange / etc — ignored for now.
    }
}

void ParameterInterface::emit_note_off(uint8_t note, ParameterData& out)
{
    for (auto& slot : this->active_notes_)
    {
        if (slot.active && slot.note == note)
        {
            if (out.midi_event_count < ParameterData::max_midi_events_per_block)
            {
                out.midi_events[out.midi_event_count++] = MidiNoteEvent {
                    /*note_on=*/false,
                    /*midi_seq=*/slot.midi_seq,
                    /*velocity=*/0.f,
                    /*note_ratio=*/0.f,
                    /*note_number=*/0,
                };
            }
            slot = ActiveNote{};  // clear
            return;               // one note-off → one voice release
        }
    }
}

int ParameterInterface::allocate_midi_voice_slot(bool voice_stealing)
{
    // First inactive slot wins.
    for (size_t s = 0; s < this->active_notes_.size(); ++s)
    {
        if (!this->active_notes_[s].active) return static_cast<int>(s);
    }
    // All slots full. The audio-side VoiceAllocator picks the actual voice
    // to steal by launch_seq, so any slot here is fine — we just need *a*
    // mapping for note-off matching, and overwriting an existing slot means
    // the previously-stolen note's note-off becomes a no-op (the voice it
    // referred to is already gone). Pick slot 0 for determinism.
    if (voice_stealing) return 0;
    return -1;
}
