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
        out.start  = idsp::clamp(p.start  + bipolar_rand(rng_state) * p.random_start,  0.f, 1.f);
        out.length = idsp::clamp(p.length + bipolar_rand(rng_state) * p.random_length, 0.f, 1.f);
        out.speed  = p.speed + bipolar_rand(rng_state) * p.random_speed * 8.f;
        out.level  = p.level;   // per-grain jitter applied inside Voice
        out.pan    = p.pan;     // per-grain jitter applied inside Voice

        out.attack  = p.attack;
        out.decay   = p.decay;
        out.sustain = p.sustain;
        out.release = p.release;

        out.envelope_speed  = p.envelope_speed;
        out.envelope_start  = p.envelope_start;
        out.envelope_length = p.envelope_length;
        out.envelope_level  = p.envelope_level;
        out.envelope_pan    = p.envelope_pan;

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
        return out;
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

        out.envelope_speed  = p.envelope_speed;
        out.envelope_start  = p.envelope_start;
        out.envelope_length = p.envelope_length;
        out.envelope_level  = p.envelope_level;
        out.envelope_pan    = p.envelope_pan;

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
        return out;
    }
}

Instrument::Instrument(InstrumentOutputData& output)
{
}

void Instrument::load(const InstrumentLoadData& loaded, InstrumentOutputData& output)
{
}

void Instrument::prepare(double sample_rate)
{
    this->sample_rate_ = static_cast<float>(sample_rate);
}

void Instrument::process(const InstrumentInputData& input, InstrumentOutputData& output)
{
    const size_t block_size  = output.audio.channel(0).size();
    const size_t buffer_size = input.buffer.loaded_sample.channel(0).size();
    if (buffer_size == 0) return;

    const auto& p = input.parameter;

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
        // Voice mode: overlay live edits onto the selected slot; stop kills it.
        voice_live_params_[selected_voice] = overlay_live_params(p);
        if (p.stop) voices_[selected_voice].kill();
    }
    else
    {
        // Global mode: overlay live edits onto every active voice; stop kills all.
        const VoiceLiveParams overlay = overlay_live_params(p);
        for (size_t i = 0; i < voices_.size(); ++i)
        {
            if (voices_[i].is_active()) voice_live_params_[i] = overlay;
        }
        if (p.stop) voices_.kill_all();
    }

    // --- play: allocate a plain (no envelope) voice. Voice mode forces it
    //     into the selected slot; Global mode picks any free slot. The voice
    //     loops indefinitely until stopped. ---
    if (p.play)
    {
        const int preferred = voice_selected ? selected_voice : -1;
        if (Voice* v = allocator_.acquire(voices_, p.voice_stealing, preferred))
        {
            const size_t slot = static_cast<size_t>(v - &voices_[0]);
            voice_live_params_[slot] = build_effective_live_params(p, rng_state_);
            v->trigger_plain(voice_live_params_[slot], buffer_size, sample_rate_, ++launch_counter_);
            voice_midi_seq_[slot] = 0;
        }
        // else: fail-silent (stealing off, all voices busy)
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
            if (Voice* v = allocator_.acquire(voices_, p.voice_stealing, -1))
            {
                const size_t slot = static_cast<size_t>(v - &voices_[0]);
                // Slider-side live params only. The MIDI per-note offsets
                // (pitch octave shift, velocity factor) live inside Voice
                // and are combined with these slider values during set_live_params
                // (pitch / speed) and per-grain (level). Keeping MIDI offsets
                // out of voice_live_params keeps the slider snapshot clean
                // and avoids any multiply-compounding on snap-on-select.
                voice_live_params_[slot] = build_effective_live_params(p, rng_state_);
                v->trigger_adsr_gated(voice_live_params_[slot], buffer_size, sample_rate_, ++launch_counter_);
                const float octave_offset = std::log2(ev.note_ratio > 0.f ? ev.note_ratio : 1.f);
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
    if (p.position_scrubbing)
    {
        const float frac = idsp::clamp(p.position, 0.f, 1.f);
        auto scrub = [&](size_t i)
        {
            if (voices_[i].is_active()) voices_[i].set_loop_position_fraction(frac);
        };
        if (voice_selected && voices_[selected_voice].is_active())
        {
            scrub(static_cast<size_t>(selected_voice));
        }
        else if (p.global_mode)
        {
            for (size_t i = 0; i < voices_.size(); ++i) scrub(i);
        }
        else
        {
            int target = -1;
            uint64_t best = 0;
            for (size_t i = 0; i < voices_.size(); ++i)
            {
                if (voices_[i].is_active() && voices_[i].launch_seq() >= best)
                {
                    best   = voices_[i].launch_seq();
                    target = static_cast<int>(i);
                }
            }
            if (target >= 0) scrub(static_cast<size_t>(target));
        }
    }

    // --- envelope_trigger button: always spawns a new AR voice. ---
    if (p.envelope_trigger)
    {
        const int preferred = voice_selected ? selected_voice : -1;
        if (Voice* v = allocator_.acquire(voices_, p.voice_stealing, preferred))
        {
            const size_t slot = static_cast<size_t>(v - &voices_[0]);
            voice_live_params_[slot] = build_effective_live_params(p, rng_state_);
            v->trigger_ar(voice_live_params_[slot], buffer_size, sample_rate_, ++launch_counter_);
        }
    }

    // --- push live edits into each active voice once per block ---
    for (size_t vi = 0; vi < voices_.size(); ++vi)
    {
        if (voices_[vi].is_active())
            voices_[vi].set_live_params(voice_live_params_[vi], buffer_size, sample_rate_);
    }

    // --- zero output, then sum each voice voice-major (cheaper iteration). ---
    for (size_t i = 0; i < block_size; ++i)
    {
        output.audio.channel(0)[i] = 0.0f;
        output.audio.channel(1)[i] = 0.0f;
    }

    const auto& left_buffer  = input.buffer.sample[0];
    const auto& right_buffer = input.buffer.sample[1];

    for (size_t vi = 0; vi < voices_.size(); ++vi)
    {
        auto& v = voices_[vi];
        if (!v.is_active()) continue;
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
        output.state.voice_live_params[i] = voice_live_params_[i];
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
