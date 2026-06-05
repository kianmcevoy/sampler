#include "instrument/instrument.hpp"

#include "idsp/functions.hpp"

#include <cmath>
#include <cstddef>

namespace
{
    inline uint32_t xorshift32(uint32_t& s)
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }

    // Bipolar uniform in [-1, +1]. 24 random bits → float; bias is well below
    // anything audible at the jitter amounts this gets used for.
    inline float bipolar_rand(uint32_t& s)
    {
        return static_cast<float>(xorshift32(s) >> 8) * (1.f / 8388608.f) - 1.f;
    }

    // Apply per-launch random_* jitter to the supplied global params and
    // return the resulting "effective" live params for a new voice.
    // Granular deviations are passed through unchanged — random for those
    // is per-grain inside Voice, not per-launch.
    VoiceLiveParams build_effective_live_params(const ParameterData& p, uint32_t& rng_state)
    {
        VoiceLiveParams out;
        if (p.markers_enabled && p.marker_count > 0)
        {
            // Marker mode: jitter the integer marker index (rounded), so a
            // launch always lands on a marker. random_start=1 spans the whole
            // marker grid. Length jitters in marker counts the same way.
            const int N = p.marker_count;
            const int start_jit = static_cast<int>(std::round(
                bipolar_rand(rng_state) * p.random_start * static_cast<float>(N - 1)));
            const int s_marker = idsp::clamp(p.start_marker + start_jit, 0, N - 1);
            out.start = p.marker_fractions[s_marker];

            const int length_jit = static_cast<int>(std::round(
                bipolar_rand(rng_state) * p.random_length * static_cast<float>(N - 1)));
            const int span = idsp::clamp(p.length_markers + length_jit, 1, N - s_marker);
            const int end_idx = s_marker + span;  // exclusive
            const float end_frac = (end_idx < N) ? p.marker_fractions[end_idx] : 1.f;
            out.length = end_frac - out.start;
        }
        else
        {
            out.start  = idsp::clamp(p.start  + bipolar_rand(rng_state) * p.random_start,  0.f, 1.f);
            out.length = idsp::clamp(p.length + bipolar_rand(rng_state) * p.random_length, 0.f, 1.f);
        }
        out.speed  = p.speed + bipolar_rand(rng_state) * p.random_speed * 8.f;
        out.level  = p.level;   // per-grain jitter applied inside Voice
        out.pan    = p.pan;     // per-grain jitter applied inside Voice

        out.attack  = p.attack;
        out.decay   = p.decay;
        out.sustain = p.sustain;
        out.release = p.release;

        // Cutoff jitter is log-scale (multiplicative in octaves) so it sounds
        // uniform across the frequency range. ±3 oct max at random_cutoff = 1.
        out.filter_freq = idsp::clamp(
            p.filter_freq * std::exp2(bipolar_rand(rng_state) * p.random_cutoff * 3.f),
            20.f, 20000.f);
        out.filter_q    = p.filter_q;

        out.envelope_speed  = p.envelope_speed;
        out.envelope_start  = p.envelope_start;
        out.envelope_length = p.envelope_length;
        out.envelope_level  = p.envelope_level;
        out.envelope_pan    = p.envelope_pan;
        out.envelope_cutoff = p.envelope_cutoff;
        out.envelope_resonance = p.envelope_resonance;

        out.phase_speed  = p.phase_speed;
        out.phase_start  = p.phase_start;
        out.phase_length = p.phase_length;
        out.phase_level  = p.phase_level;
        out.phase_pan    = p.phase_pan;

        out.pitch_deviation  = p.pitch_deviation;
        out.size_deviation   = p.size_deviation;
        out.shape_deviation  = p.shape_deviation;
        out.grains_deviation = p.grains_deviation;
        out.timestretch      = p.timestretch;
        out.scale_envelope   = p.scale_envelope;
        out.random_pitch     = p.random_pitch;
        out.random_size      = p.random_size;
        out.random_shape     = p.random_shape;
        out.random_grains    = p.random_grains;
        out.random_position  = p.random_position;
        out.random_level     = p.random_level;
        out.random_pan       = p.random_pan;
        return out;
    }

    // Canonical slider ranges for the 5 playback params, sourced from
    // gui/src/controls.cpp. The scaling pickup formula needs each slider's
    // (min, max) to compute convergence at the extremes. If GUI ranges
    // change, mirror them here.
    constexpr float kStartMin  = 0.f, kStartMax  = 1.f;
    constexpr float kLengthMin = 0.f, kLengthMax = 1.f;
    constexpr float kSpeedMin  = -4.f, kSpeedMax = 4.f;
    constexpr float kLevelMin  = 0.f, kLevelMax  = 1.f;
    constexpr float kPanMin    = 0.f, kPanMax    = 1.f;

    // Piecewise-linear scaling pickup. Maps the live slider value into an
    // effective voice value such that the curve passes through
    // (anchor_slider, anchor_voice) and converges to (min, min) and
    // (max, max) at the slider's extremes. Returning the slider to its
    // anchor restores the voice's original (post-random) value.
    inline float scale_to_anchor(float slider_now, float anchor_slider,
                                 float anchor_voice, float slider_min, float slider_max)
    {
        if (slider_now >= anchor_slider)
        {
            const float span = slider_max - anchor_slider;
            if (span <= 1e-9f) return anchor_voice;
            const float t = (slider_now - anchor_slider) / span;
            return anchor_voice + (slider_max - anchor_voice) * t;
        }
        else
        {
            const float span = anchor_slider - slider_min;
            if (span <= 1e-9f) return anchor_voice;
            const float t = (anchor_slider - slider_now) / span;
            return anchor_voice + (slider_min - anchor_voice) * t;
        }
    }

    // Snapshot the current slider state for the 5 playback params.
    VoiceSliderAnchor capture_anchor(const ParameterData& p)
    {
        return { p.start, p.length, p.speed, p.level, p.pan };
    }

    // Pull the live-editable subset of the global params into a slot. Used
    // when the user is live-editing the currently selected voice — random
    // offsets are NOT applied here, those only fire on a fresh trigger.
    VoiceLiveParams overlay_live_params(const ParameterData& p)
    {
        VoiceLiveParams out;
        out.start  = p.start;
        out.length = p.length;
        out.speed  = p.speed;
        out.level  = p.level;
        out.pan    = p.pan;

        out.attack  = p.attack;
        out.decay   = p.decay;
        out.sustain = p.sustain;
        out.release = p.release;

        out.filter_freq = p.filter_freq;
        out.filter_q    = p.filter_q;

        out.envelope_speed  = p.envelope_speed;
        out.envelope_start  = p.envelope_start;
        out.envelope_length = p.envelope_length;
        out.envelope_level  = p.envelope_level;
        out.envelope_pan    = p.envelope_pan;
        out.envelope_cutoff = p.envelope_cutoff;
        out.envelope_resonance = p.envelope_resonance;

        out.phase_speed  = p.phase_speed;
        out.phase_start  = p.phase_start;
        out.phase_length = p.phase_length;
        out.phase_level  = p.phase_level;
        out.phase_pan    = p.phase_pan;

        out.pitch_deviation  = p.pitch_deviation;
        out.size_deviation   = p.size_deviation;
        out.shape_deviation  = p.shape_deviation;
        out.grains_deviation = p.grains_deviation;
        out.timestretch      = p.timestretch;
        out.scale_envelope   = p.scale_envelope;
        out.random_pitch     = p.random_pitch;
        out.random_size      = p.random_size;
        out.random_shape     = p.random_shape;
        out.random_grains    = p.random_grains;
        out.random_position  = p.random_position;
        out.random_level     = p.random_level;
        out.random_pan       = p.random_pan;
        return out;
    }

    // Overlay only the non-playback live fields onto an existing snapshot.
    // Used in Global mode so the 5 playback params (start/length/speed/level/
    // pan) stay frozen at their trigger-time effective values — the scaling
    // pickup at set_live_params time will derive the live playback values
    // from the slider + per-voice anchor instead.
    void overlay_non_playback_fields(VoiceLiveParams& out, const ParameterData& p)
    {
        out.attack  = p.attack;
        out.decay   = p.decay;
        out.sustain = p.sustain;
        out.release = p.release;

        out.filter_freq = p.filter_freq;
        out.filter_q    = p.filter_q;

        out.envelope_speed  = p.envelope_speed;
        out.envelope_start  = p.envelope_start;
        out.envelope_length = p.envelope_length;
        out.envelope_level  = p.envelope_level;
        out.envelope_pan    = p.envelope_pan;
        out.envelope_cutoff = p.envelope_cutoff;
        out.envelope_resonance = p.envelope_resonance;

        out.phase_speed  = p.phase_speed;
        out.phase_start  = p.phase_start;
        out.phase_length = p.phase_length;
        out.phase_level  = p.phase_level;
        out.phase_pan    = p.phase_pan;

        out.pitch_deviation  = p.pitch_deviation;
        out.size_deviation   = p.size_deviation;
        out.shape_deviation  = p.shape_deviation;
        out.grains_deviation = p.grains_deviation;
        out.timestretch      = p.timestretch;
        out.random_pitch     = p.random_pitch;
        out.random_size      = p.random_size;
        out.random_shape     = p.random_shape;
        out.random_grains    = p.random_grains;
        out.random_position  = p.random_position;
        out.random_level     = p.random_level;
        out.random_pan       = p.random_pan;
    }
}

Instrument::Instrument(InstrumentOutputData& /*output*/)
{
}

void Instrument::load(const InstrumentLoadData& /*loaded*/, InstrumentOutputData& /*output*/)
{
}

void Instrument::prepare(double sample_rate)
{
    this->sample_rate_ = static_cast<float>(sample_rate);
    this->sample_rate_inv_ = 1.f / sample_rate_;

    for(auto& v : voice_live_params_)
    {
        v.sample_rate_inv = this->sample_rate_inv_;
    }
}

void Instrument::process(const InstrumentInputData& input, InstrumentOutputData& output)
{
    const size_t block_size  = output.audio.channel(0).size();
    const auto& p = input.parameter;

    // Pick the editing layer's buffer for slider-driven logic (new triggers,
    // marker-snap reads, etc.). May be 0 if the currently selected layer has
    // no sample loaded — in that case, new triggers are silently dropped but
    // existing voices on other layers keep playing.
    const int editing_layer = idsp::clamp(p.current_layer, 0, static_cast<int>(max_layers) - 1);
    const size_t buffer_size = input.layer_buffers[editing_layer].loaded_sample.channel(0).size();

    // When the editing layer changes, the GUI has just restored a saved slider
    // snapshot for the new layer. Re-anchor every active voice on that layer so
    // the scaling-pickup formula sees zero displacement and leaves them untouched.
    if (editing_layer != prev_editing_layer_)
    {
        const auto new_anchor = capture_anchor(p);
        for (size_t i = 0; i < voices_.size(); ++i)
        {
            if (voices_[i].is_active() && voices_[i].layer() == editing_layer)
                voice_anchor_[i] = new_anchor;
        }
        prev_editing_layer_ = editing_layer;
    }

    // Clear MIDI ownership + latch state for any voice that became inactive
    // since last block (natural envelope completion, stop, or voice-stealing).
    // If we skip this, a recycled slot could erroneously match a stale note-off
    // or carry a stale latch flag into the next voice that lands in this slot.
    for (size_t s = 0; s < voices_.size(); ++s)
    {
        if (!voices_[s].is_active())
        {
            voice_midi_seq_[s] = 0;
            voice_latched_[s]  = false;
        }
    }

    const int selected_voice = p.selected_voice;
    const bool voice_selected = (selected_voice >= 0 && static_cast<size_t>(selected_voice) < max_voices);

    // --- live-edit + stop: two-state mux (Voice / Global). Global is the
    //     default whenever no voice is selected. ---
    if (voice_selected && voices_[selected_voice].is_active())
    {
        // Voice mode: overlay live edits onto the selected slot (jump-to);
        // re-snap the anchor so a later switch to Global mode resumes
        // scaling from the latest Voice-mode value, not the stale trigger.
        voice_live_params_[selected_voice] = overlay_live_params(p);
        voice_anchor_[selected_voice]      = capture_anchor(p);
        if (p.stop) voices_[selected_voice].kill();
    }
    else
    {
        // Global mode: overlay only the non-playback live fields onto each
        // active voice ON THE CURRENT LAYER. Voices on other layers (e.g. a
        // held / latched MIDI voice triggered while editing a different
        // layer) keep their trigger-time values so they don't mutate when
        // the user switches layer. The 5 playback params stay at their
        // trigger-time effective values in voice_live_params_; scaling
        // pickup is applied later, in the per-voice set_live_params loop.
        for (size_t i = 0; i < voices_.size(); ++i)
        {
            if (voices_[i].is_active() && voices_[i].layer() == editing_layer)
                overlay_non_playback_fields(voice_live_params_[i], p);
        }
        // Stop scopes to the current layer's voices only — leaves voices on
        // other layers alone. Stop All (handled below) is the cross-layer kill.
        if (p.stop)
        {
            for (size_t i = 0; i < voices_.size(); ++i)
            {
                if (voices_[i].is_active() && voices_[i].layer() == editing_layer)
                    voices_[i].kill();
            }
        }
    }

    // --- play: allocate a plain (no envelope) voice. Voice mode forces it
    //     into the selected slot; Global mode picks any free slot. The voice
    //     loops indefinitely until stopped. ---
    if (p.play && buffer_size > 0)
    {
        const int preferred = voice_selected ? selected_voice : -1;
        if (Voice* v = allocator_.acquire(voices_, p.voice_stealing, preferred))
        {
            const size_t slot = static_cast<size_t>(v - &voices_[0]);
            voice_live_params_[slot]      = build_effective_live_params(p, rng_state_);
            voice_effective_params_[slot] = voice_live_params_[slot];
            voice_anchor_[slot]           = capture_anchor(p);
            v->trigger_plain(voice_live_params_[slot], buffer_size, sample_rate_, ++launch_counter_);
            v->set_sample_loops(p.loop);
            v->set_layer(editing_layer);
            voice_midi_seq_[slot] = 0;
        }
        // else: fail-silent (stealing off, all voices busy)
    }

    // Stop All — kill every voice on every layer, regardless of mode.
    if (p.stop_all) voices_.kill_all();

    // --- Touch trigger events ---
    // Each event launches a voice with explicit (start, level, layer) overrides.
    // Mirrors the `play` path otherwise — voice_anchor is captured for the
    // event's overridden values so Global-mode scaling-pickup still behaves
    // sensibly relative to where the touch launched it.
    for (size_t i = 0; i < p.touch_event_count; ++i)
    {
        const auto& ev = p.touch_events[i];
        const int target_layer = idsp::clamp(ev.target_layer, 0, static_cast<int>(max_layers) - 1);
        const auto& layer_buf  = input.layer_buffers[static_cast<size_t>(target_layer)];
        const int   tgt_buffer_size = static_cast<int>(layer_buf.loaded_sample.channel(0).size());
        if (tgt_buffer_size == 0) continue;  // empty layer — drop

        if (Voice* v = allocator_.acquire(voices_, p.voice_stealing, -1))
        {
            const size_t slot = static_cast<size_t>(v - &voices_[0]);
            voice_live_params_[slot] = build_effective_live_params(p, rng_state_);
            // Override start + level with the touch values, preserving every
            // other slider's contribution (granular shape, envelope, etc.).
            voice_live_params_[slot].start = idsp::clamp(ev.start_fraction, 0.f, 1.f);
            voice_live_params_[slot].level = idsp::clamp(ev.level,          0.f, 1.f);
            voice_effective_params_[slot] = voice_live_params_[slot];
            // Anchor for Global-mode scaling-pickup uses the touch-overridden
            // values so the slider returns the voice to its launched values.
            voice_anchor_[slot]       = capture_anchor(p);
            voice_anchor_[slot].start = voice_live_params_[slot].start;
            voice_anchor_[slot].level = voice_live_params_[slot].level;

            v->trigger_plain(voice_live_params_[slot],
                             tgt_buffer_size, sample_rate_, ++launch_counter_);
            v->set_sample_loops(p.loop);
            v->set_layer(target_layer);
            voice_midi_seq_[slot] = 0;
        }
        // else: fail-silent.
    }

    // --- MIDI note events ---
    // Note-ons allocate a voice and trigger a gated ADSR; the matching
    // note-off finds the voice via midi_seq and calls release(), which
    // transitions the envelope into its release stage from the current value.
    for (size_t i = 0; i < p.midi_event_count; ++i)
    {
        const auto& ev = p.midi_events[i];
        if (ev.note_on)
        {
            if (buffer_size == 0) continue;  // no sample on current layer — drop
            if (Voice* v = allocator_.acquire(voices_, p.voice_stealing, -1))
            {
                const size_t slot = static_cast<size_t>(v - &voices_[0]);
                // Slider-side live params only. The MIDI per-note offsets
                // (pitch octave shift, velocity factor) live inside Voice
                // and are combined with these slider values during set_live_params
                // (pitch / speed) and per-grain (level). Keeping MIDI offsets
                // out of voice_live_params keeps the slider snapshot clean
                // and avoids any multiply-compounding on snap-on-select.
                voice_live_params_[slot]      = build_effective_live_params(p, rng_state_);
                voice_effective_params_[slot] = voice_live_params_[slot];
                voice_anchor_[slot]           = capture_anchor(p);

                // Position routing: the MIDI note picks a start fraction
                // (snapped to a marker when markers are active), overriding
                // any random-jittered start that build_effective_live_params
                // produced. Length is re-anchored from the new start.
                if (p.note_route_mode == 1)
                {
                    float start_frac = 0.f;
                    float length_frac = idsp::clamp(p.length, 0.f, 1.f);
                    if (p.markers_enabled && p.marker_count > 0)
                    {
                        const int N    = p.marker_count;
                        const int base = idsp::clamp(ev.note_number - 60, 0, N - 1);
                        // Jitter the note-selected marker index so random_start
                        // picks a randomised nearby slice rather than always
                        // the exact note-mapped one.
                        const int jit = static_cast<int>(std::round(
                            bipolar_rand(rng_state_) * p.random_start * static_cast<float>(N - 1)));
                        const int idx  = idsp::clamp(base + jit, 0, N - 1);
                        start_frac     = p.marker_fractions[idx];
                        const int span    = idsp::clamp(p.length_markers, 1, N - idx);
                        const int end_idx = idx + span;
                        const float end   = (end_idx < N) ? p.marker_fractions[end_idx] : 1.f;
                        length_frac = end - start_frac;
                    }
                    else
                    {
                        start_frac  = idsp::clamp(
                            static_cast<float>(ev.note_number) / 127.f
                            + bipolar_rand(rng_state_) * p.random_start,
                            0.f, 1.f);
                        length_frac = idsp::clamp(p.length, 0.f, 1.f - start_frac);
                    }
                    voice_live_params_[slot].start  = start_frac;
                    voice_live_params_[slot].length = length_frac;
                    voice_effective_params_[slot]   = voice_live_params_[slot];
                }

                if (p.scale_envelope)
                    v->trigger_ahr(voice_live_params_[slot], buffer_size, sample_rate_, ++launch_counter_);
                else
                    v->trigger_adsr_gated(voice_live_params_[slot], buffer_size, sample_rate_, ++launch_counter_);
                v->set_sample_loops(p.loop);
                v->set_layer(editing_layer);
                // Pitch routing applies the semitone offset; position routing
                // suppresses it so the note number doesn't double up as both
                // a pitch shift and a start selector.
                const float octave_offset = (p.note_route_mode == 1)
                    ? 0.f
                    : std::log2(ev.note_ratio > 0.f ? ev.note_ratio : 1.f);
                v->set_midi_offsets(octave_offset, ev.velocity);
                voice_midi_seq_[slot] = ev.midi_seq;
            }
            // else: all voices busy, stealing off — drop.
        }
        else
        {
            // note-off: locate the voice that owns this MIDI seq. If the
            // voice was latched, swallow the note-off (voice keeps looping
            // until stop). Otherwise release the envelope normally.
            for (size_t s = 0; s < voices_.size(); ++s)
            {
                if (voice_midi_seq_[s] == ev.midi_seq && voices_[s].is_active())
                {
                    if (voice_latched_[s])
                    {
                        voice_midi_seq_[s] = 0;
                        voice_latched_[s]  = false;
                    }
                    else
                    {
                        voices_[s].release();
                        voice_midi_seq_[s] = 0;
                    }
                    break;
                }
            }
        }
    }

    // --- latch trigger: mark every still-gated MIDI voice as latched so
    //     that its next note-off is swallowed. Voices already in release
    //     (voice_midi_seq_ == 0) are skipped. ---
    if (p.latch)
    {
        for (size_t i = 0; i < voices_.size(); ++i)
        {
            if (voices_[i].is_active() && voice_midi_seq_[i] != 0)
                voice_latched_[i] = true;
        }
    }

    // --- position scrub: when the GUI signals the user is dragging the
    //     position pot, teleport the routing-target voice(s) to the slider
    //     value. Gated by the GUI's gesture flag so the audio→GUI display
    //     loop doesn't masquerade as user input. Voice mode → selected,
    //     Global → all, Auto → newest. Scrub is a teleport — `speed` resumes.
    // Touch scrub — runs unconditionally (not gated by p.position_scrubbing,
    // since the GUI publishes it via its own gesture flag). Always wins over
    // the position-slider scrub for the targeted slot; non-targeted slots are
    // unaffected here and may still be scrubbed by the position-slider path
    // below.
    if (p.touch_scrub_slot >= 0
        && p.touch_scrub_slot < static_cast<int>(voices_.size())
        && voices_[static_cast<size_t>(p.touch_scrub_slot)].is_active())
    {
        const float pos = idsp::clamp(p.touch_scrub_position, 0.f, 1.f);
        voices_[static_cast<size_t>(p.touch_scrub_slot)].set_loop_position_fraction(pos);
    }

    if (p.position_scrubbing)
    {
        const float frac = idsp::clamp(p.position, 0.f, 1.f);
        if (voice_selected && voices_[selected_voice].is_active())
        {
            // Voice mode: jump-to on the selected voice.
            voices_[selected_voice].set_loop_position_fraction(frac);
        }
        else if (p.global_mode)
        {
            // Global mode: value-scaling pickup, scoped to the current layer.
            // On the rising edge of the scrub gesture, snapshot the slider
            // value and each in-layer voice's current loop position. While
            // the drag continues, rescale each voice's position through its
            // anchor — returning the slider to its grab point restores
            // every voice's relative phase. Voices on other layers are
            // untouched.
            if (!position_scrubbing_prev_)
            {
                position_slider_anchor_ = frac;
                for (size_t i = 0; i < voices_.size(); ++i)
                {
                    voice_position_anchor_[i] = (voices_[i].is_active()
                                                 && voices_[i].layer() == editing_layer)
                        ? voices_[i].loop_position_fraction()
                        : 0.f;
                }
            }
            for (size_t i = 0; i < voices_.size(); ++i)
            {
                if (!voices_[i].is_active()) continue;
                if (voices_[i].layer() != editing_layer) continue;
                const float scaled = scale_to_anchor(frac,
                                                     position_slider_anchor_,
                                                     voice_position_anchor_[i],
                                                     0.f, 1.f);
                voices_[i].set_loop_position_fraction(idsp::clamp(scaled, 0.f, 1.f));
            }
        }
        else
        {
            // Auto mode: jump-to on the newest active voice on the current layer.
            int target = -1;
            uint64_t best = 0;
            for (size_t i = 0; i < voices_.size(); ++i)
            {
                if (voices_[i].is_active() && voices_[i].layer() == editing_layer
                    && voices_[i].launch_seq() >= best)
                {
                    best   = voices_[i].launch_seq();
                    target = static_cast<int>(i);
                }
            }
            if (target >= 0) voices_[static_cast<size_t>(target)].set_loop_position_fraction(frac);
        }
    }
    position_scrubbing_prev_ = p.position_scrubbing;

    // --- envelope_trigger button: always spawns a new AR voice. ---
    if (p.envelope_trigger && buffer_size > 0)
    {
        const int preferred = voice_selected ? selected_voice : -1;
        if (Voice* v = allocator_.acquire(voices_, p.voice_stealing, preferred))
        {
            const size_t slot = static_cast<size_t>(v - &voices_[0]);
            voice_live_params_[slot]      = build_effective_live_params(p, rng_state_);
            voice_effective_params_[slot] = voice_live_params_[slot];
            voice_anchor_[slot]           = capture_anchor(p);
            if (p.scale_envelope)
                v->trigger_ahr(voice_live_params_[slot], buffer_size, sample_rate_, ++launch_counter_);
            else
                v->trigger_ar(voice_live_params_[slot], buffer_size, sample_rate_, ++launch_counter_);
            v->set_layer(editing_layer);
        }
    }

    // --- push live edits into each active voice once per block. In Global
    //     mode the 5 playback params are rescaled per voice through its
    //     anchor (scaling pickup); in Voice mode voice_live_params_ already
    //     reflects the user's jump-to edit verbatim.
    const bool global_mode_now = !(voice_selected && voices_[selected_voice].is_active());
    for (size_t vi = 0; vi < voices_.size(); ++vi)
    {
        if (!voices_[vi].is_active()) continue;

        // Each voice's set_live_params is scaled against its own layer's
        // buffer length (which may differ from the editing layer's).
        const size_t voice_buffer_size =
            input.layer_buffers[voices_[vi].layer()].loaded_sample.channel(0).size();
        if (voice_buffer_size == 0) continue;

        VoiceLiveParams effective;
        if (!global_mode_now && static_cast<int>(vi) == selected_voice)
        {
            // Voice mode on the selected slot: jump-to from voice_live_params_
            // (which the mode-mux at the top already overwrote with current
            // slider values).
            effective = voice_live_params_[vi];
            voice_effective_params_[vi] = effective;
        }
        else if (global_mode_now && voices_[vi].layer() == editing_layer)
        {
            // On-layer in Global mode: scaling pickup against the trigger-time
            // anchor for the 5 playback params, slider verbatim for the rest.
            effective = voice_live_params_[vi];
            const auto& anchor = voice_anchor_[vi];
            const auto& trig   = voice_live_params_[vi];
            effective.start  = scale_to_anchor(p.start,  anchor.start,  trig.start,  kStartMin,  kStartMax);
            effective.length = scale_to_anchor(p.length, anchor.length, trig.length, kLengthMin, kLengthMax);
            effective.speed  = scale_to_anchor(p.speed,  anchor.speed,  trig.speed,  kSpeedMin,  kSpeedMax);
            effective.level  = scale_to_anchor(p.level,  anchor.level,  trig.level,  kLevelMin,  kLevelMax);
            effective.pan    = scale_to_anchor(p.pan,    anchor.pan,    trig.pan,    kPanMin,    kPanMax);
            voice_effective_params_[vi] = effective;
        }
        else
        {
            // Off-layer in Global mode (or any other untouched case):
            // replay whatever was last actually pushed. Keeps held voices
            // playing the user's last-edited values across layer switches.
            effective = voice_effective_params_[vi];
        }

        // Touch scrub: override the level for the targeted slot, regardless
        // of mode. The position teleport landed earlier in the scrub block;
        // here we just keep the level sync'd to the finger's Y position.
        if (p.touch_scrub_slot >= 0
            && static_cast<int>(vi) == p.touch_scrub_slot)
        {
            effective.level = idsp::clamp(p.touch_scrub_level, 0.f, 1.f);
            voice_effective_params_[vi] = effective;
        }

        voices_[vi].set_live_params(effective, voice_buffer_size, sample_rate_);
    }

    // --- zero output, then sum each voice voice-major (cheaper iteration). ---
    for (size_t i = 0; i < block_size; ++i)
    {
        output.audio.channel(0)[i] = 0.0f;
        output.audio.channel(1)[i] = 0.0f;
    }

    for (size_t vi = 0; vi < voices_.size(); ++vi)
    {
        auto& v = voices_[vi];
        if (!v.is_active()) continue;
        // Each voice reads from its own layer's playback buffer for life.
        const auto& lbuf = input.layer_buffers[v.layer()];
        const auto& left_buffer  = lbuf.sample[0];
        const auto& right_buffer = lbuf.sample[1];
        for (size_t i = 0; i < block_size; ++i)
        {
            if (!v.is_active()) break;
            const auto f = v.process(left_buffer, right_buffer);
            output.audio.channel(0)[i] += f.l;
            output.audio.channel(1)[i] += f.r;
        }
    }

    // --- publish display state ---
    output.state.playback_position = voices_[0].is_active() ? voices_[0].position() : -1.0f;
    for (size_t i = 0; i < max_voices; ++i)
    {
        output.state.voice_active[i]      = voices_[i].is_active();
        output.state.voice_position[i]    = voices_[i].position();
        output.state.voice_volume[i]      = voices_[i].current_level();
        output.state.voice_layer[i]       = voices_[i].layer();
        output.state.voice_live_params[i] = voice_live_params_[i];
    }

    // Aggregate per-layer view state. Each active voice contributes its
    // current_level to its layer's slot; the GUI uses this for layer-view
    // button brightness and the "any voice alive on this layer" flag.
    for (size_t li = 0; li < max_layers; ++li)
    {
        output.state.layer_has_active_voices[li] = false;
        output.state.layer_summed_envelope[li]   = 0.f;
    }
    for (size_t i = 0; i < max_voices; ++i)
    {
        if (!voices_[i].is_active()) continue;
        const int li = idsp::clamp(voices_[i].layer(), 0, static_cast<int>(max_layers) - 1);
        output.state.layer_has_active_voices[li] = true;
        output.state.layer_summed_envelope[li] += voices_[i].current_level();
    }

    // Position-slider display: pick the same routing-target voice as scrub
    // (Voice → selected, Global → first active, Auto → newest) and publish
    // its normalized loop position. If no voice is active, hold the last
    // published value so the slider doesn't snap.
    int display_voice = -1;
    if (voice_selected && voices_[selected_voice].is_active())
    {
        display_voice = selected_voice;
    }
    else if (p.global_mode)
    {
        for (size_t i = 0; i < voices_.size(); ++i)
        {
            if (voices_[i].is_active()) { display_voice = static_cast<int>(i); break; }
        }
    }
    else
    {
        uint64_t best = 0;
        for (size_t i = 0; i < voices_.size(); ++i)
        {
            if (voices_[i].is_active() && voices_[i].launch_seq() >= best)
            {
                best          = voices_[i].launch_seq();
                display_voice = static_cast<int>(i);
            }
        }
    }
    if (display_voice >= 0)
    {
        output.state.playback_position_normalized = voices_[display_voice].loop_position_fraction();
    }
    // else: leave whatever was last published (no active voice means there's
    // nothing meaningful to update with).
}
