#include "instrument/instrument.hpp"

#include "idsp/functions.hpp"

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
    VoiceLiveParams build_effective_live_params(const ParameterData& p, uint32_t& rng_state)
    {
        VoiceLiveParams out;
        // random_speed is scaled to span the full ±4 speed range.
        out.start  = idsp::clamp(p.start  + bipolar_rand(rng_state) * p.random_start,  0.f, 1.f);
        out.length = idsp::clamp(p.length + bipolar_rand(rng_state) * p.random_length, 0.f, 1.f);
        out.speed  = p.speed + bipolar_rand(rng_state) * p.random_speed * 8.f;
        out.level  = idsp::clamp(p.level  + bipolar_rand(rng_state) * p.random_level,  0.f, 1.f);
        out.pan    = idsp::clamp(p.pan    + bipolar_rand(rng_state) * p.random_pan,    0.f, 1.f);
        out.loop   = p.loop;

        out.time          = p.time;
        out.skew          = p.skew;
        out.shape         = p.shape;
        out.loop_envelope = p.loop_envelope;
        out.envelope_sync = p.envelope_sync;

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
        out.loop   = p.loop;

        out.time          = p.time;
        out.skew          = p.skew;
        out.shape         = p.shape;
        out.loop_envelope = p.loop_envelope;
        out.envelope_sync = p.envelope_sync;

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
        return out;
    }

    // Pick the active voice with the largest launch_seq_. Returns nullptr if
    // no voice is active. Used by envelope_trigger (sync-OFF, no selection)
    // and the comparator (no selection) to follow the "newest active voice"
    // convention used elsewhere in the GUI.
    Voice* find_newest_active(VoicePool& voices)
    {
        Voice* newest = nullptr;
        uint64_t best_seq = 0;
        for (size_t i = 0; i < voices.size(); ++i)
        {
            if (voices[i].is_active() && (!newest || voices[i].launch_seq() > best_seq))
            {
                newest = &voices[i];
                best_seq = voices[i].launch_seq();
            }
        }
        return newest;
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

    // Clear MIDI ownership for any voice that became inactive since last
    // block (natural envelope completion, stop, or voice-stealing). If we
    // skip this, a recycled slot could erroneously match a stale note-off.
    for (size_t s = 0; s < voices_.size(); ++s)
    {
        if (!voices_[s].is_active()) voice_midi_seq_[s] = 0;
    }

    const int selected_voice = p.selected_voice;
    const bool voice_selected = (selected_voice >= 0 && static_cast<size_t>(selected_voice) < max_voices);

    // --- live-edit + stop: three-state mux (Voice / Global / Auto) ---
    if (voice_selected && voices_[selected_voice].is_active())
    {
        // Voice mode: overlay live edits onto the selected slot; stop kills it.
        voice_live_params_[selected_voice] = overlay_live_params(p);
        if (p.stop) voices_[selected_voice].kill();
    }
    else if (p.global_mode)
    {
        // Global mode: overlay live edits onto every active voice; stop kills all.
        const VoiceLiveParams overlay = overlay_live_params(p);
        for (size_t i = 0; i < voices_.size(); ++i)
        {
            if (voices_[i].is_active()) voice_live_params_[i] = overlay;
        }
        if (p.stop) voices_.kill_all();
    }
    else
    {
        // Auto mode: sliders feed next launch (no per-voice overlay); stop kills oldest.
        if (p.stop) voices_.kill_oldest();
    }

    // --- play: Global retriggers every active voice; Auto/Voice use the allocator ---
    if (p.play)
    {
        if (p.global_mode)
        {
            for (size_t i = 0; i < voices_.size(); ++i)
            {
                if (!voices_[i].is_active()) continue;
                voice_live_params_[i] = build_effective_live_params(p, rng_state_);
                voices_[i].trigger(voice_live_params_[i], buffer_size, sample_rate_, ++launch_counter_);
            }
        }
        else
        {
            const int preferred = voice_selected ? selected_voice : -1;
            if (Voice* v = allocator_.acquire(voices_, p.voice_stealing, preferred))
            {
                const size_t slot = static_cast<size_t>(v - &voices_[0]);
                voice_live_params_[slot] = build_effective_live_params(p, rng_state_);
                v->trigger(voice_live_params_[slot], buffer_size, sample_rate_, ++launch_counter_);
            }
            // else: fail-silent (stealing off, all voices busy)
        }
    }

    // --- MIDI note events ---
    // ParameterInterface has already parsed velocity → level, MIDI pitch →
    // speed ratio. Note-ons allocate a voice via the same allocator the play
    // button uses (respecting voice_stealing) and trigger gated so the
    // envelope holds at peak until the matching note-off arrives. Note-offs
    // find the voice via midi_seq and call release(), which transitions the
    // envelope into its release stage from wherever it currently is.
    for (size_t i = 0; i < p.midi_event_count; ++i)
    {
        const auto& ev = p.midi_events[i];
        if (ev.note_on)
        {
            if (Voice* v = allocator_.acquire(voices_, p.voice_stealing, -1))
            {
                const size_t slot = static_cast<size_t>(v - &voices_[0]);
                VoiceLiveParams vp = build_effective_live_params(p, rng_state_);
                vp.level = ev.velocity;     // already squared & scaled by slider
                vp.speed = ev.speed_ratio;  // already multiplied by slider speed
                voice_live_params_[slot] = vp;
                v->trigger(voice_live_params_[slot], buffer_size, sample_rate_, ++launch_counter_, /*gated=*/true);
                voice_midi_seq_[slot] = ev.midi_seq;
            }
            // else: all voices busy, stealing off — drop.
        }
        else
        {
            // note-off: locate the voice that owns this MIDI seq.
            for (size_t s = 0; s < voices_.size(); ++s)
            {
                if (voice_midi_seq_[s] == ev.midi_seq && voices_[s].is_active())
                {
                    voices_[s].release();
                    voice_midi_seq_[s] = 0;
                    break;
                }
            }
        }
    }

    // --- envelope_trigger: see header for the full behaviour matrix ---
    if (p.envelope_trigger)
    {
        if (p.envelope_sync)
        {
            // sync ON: (re)trigger a voice. With voice_selected the allocator
            // returns &voices_[selected_voice] unconditionally — retriggers
            // that slot whether active or not. Without selection it picks a
            // fresh slot just like p.play.
            const int preferred = voice_selected ? selected_voice : -1;
            if (Voice* v = allocator_.acquire(voices_, p.voice_stealing, preferred))
            {
                const size_t slot = static_cast<size_t>(v - &voices_[0]);
                voice_live_params_[slot] = build_effective_live_params(p, rng_state_);
                v->trigger(voice_live_params_[slot], buffer_size, sample_rate_, ++launch_counter_);
            }
        }
        else
        {
            // sync OFF: only retrigger the envelope on the selected voice
            // (if active) or the newest active voice.
            Voice* target = voice_selected
                ? (voices_[selected_voice].is_active() ? &voices_[selected_voice] : nullptr)
                : find_newest_active(voices_);
            if (target) target->trigger_envelope();
        }
    }

    // --- push live edits into each active voice once per block ---
    for (size_t vi = 0; vi < voices_.size(); ++vi)
    {
        if (voices_[vi].is_active())
            voices_[vi].set_live_params(voice_live_params_[vi], buffer_size, sample_rate_);
    }

    // --- zero output ---
    for (size_t i = 0; i < block_size; ++i)
    {
        output.audio.channel(0)[i] = 0.0f;
        output.audio.channel(1)[i] = 0.0f;
    }

    const auto& left_buffer  = input.buffer.sample[0];
    const auto& right_buffer = input.buffer.sample[1];

    // --- sample-major: process all voices per sample, then run the comparator
    // so it sees the just-computed phase/env_phase of the source voice. ---
    for (size_t i = 0; i < block_size; ++i)
    {
        for (size_t vi = 0; vi < voices_.size(); ++vi)
        {
            auto& v = voices_[vi];
            if (!v.is_active()) continue;
            const auto f = v.process(left_buffer, right_buffer);
            output.audio.channel(0)[i] += f.l;
            output.audio.channel(1)[i] += f.r;
        }

        // Comparator: source = selected voice (if active) else newest active.
        Voice* src = (voice_selected && voices_[selected_voice].is_active())
            ? &voices_[selected_voice]
            : find_newest_active(voices_);

        if (src && p.comp_threshold > 0.f)
        {
            // Bucket = how many whole thresholds fit into the current phase.
            // Fires every time we step up to a new bucket. When the source
            // wraps (phase 1→0) the bucket index drops, no fire — the next
            // upward step (at +threshold past 0) fires instead.
            float source_val = 0.0f;
            if(p.comp_source == ComparatorSource::LoopPhase)
            {
                source_val = src->phase();
            }
            else if (p.comp_source == ComparatorSource::EnvPhase)
            {
                source_val = src->env_phase();
            }
            else
            {
                source_val = 0.0f;
            }

            const int   bucket     = static_cast<int>(source_val / p.comp_threshold);
            if (bucket > comp_prev_bucket_)
            {
                if (Voice* v = allocator_.acquire(voices_, p.voice_stealing, -1))
                {
                    const size_t slot = static_cast<size_t>(v - &voices_[0]);
                    voice_live_params_[slot] = build_effective_live_params(p, rng_state_);
                    v->trigger(voice_live_params_[slot], buffer_size, sample_rate_, ++launch_counter_);
                    v->set_live_params(voice_live_params_[slot], buffer_size, sample_rate_);
                }
            }
            comp_prev_bucket_ = bucket;
        }
        // No source voice (or threshold=0): leave comp_prev_bucket_ as-is.
    }

    // --- publish display state ---
    output.state.playback_position = voices_[0].is_active() ? voices_[0].position() : -1.0f;
    for (size_t i = 0; i < max_voices; ++i)
    {
        output.state.voice_active[i]      = voices_[i].is_active();
        output.state.voice_position[i]    = voices_[i].position();
        output.state.voice_volume[i]      = voices_[i].is_active();
        output.state.voice_live_params[i] = voice_live_params_[i];
    }
}
