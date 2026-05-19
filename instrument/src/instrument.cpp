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

    // --- stop: peel off the oldest active voice ---
    if (p.stop) voices_.kill_oldest();

    // --- live edit: push the user's current slider values into the selected
    // voice's slot (only when that voice is actually active — inactive slots
    // wait until the next trigger to take on their effective params).
    const int sel = p.selected_voice;
    if (sel >= 0 && static_cast<size_t>(sel) < max_voices && voices_[sel].is_active())
    {
        voice_live_params_[sel] = overlay_live_params(p);
    }

    // --- play: allocate a voice and trigger it ---
    if (p.play)
    {
        const int preferred = (sel >= 0 && static_cast<size_t>(sel) < max_voices) ? sel : -1;
        if (Voice* v = allocator_.acquire(voices_, p.voice_stealing, preferred))
        {
            const size_t slot = static_cast<size_t>(v - &voices_[0]);
            voice_live_params_[slot] = build_effective_live_params(p, rng_state_);
            v->trigger(voice_live_params_[slot], buffer_size, sample_rate_, ++launch_counter_);
        }
        // else: fail-silent (stealing off, all voices busy)
    }

    // --- zero output, then sum each active voice's per-sample contribution ---
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

        // Push any live edits into the voice before processing this block.
        v.set_live_params(voice_live_params_[vi], buffer_size, sample_rate_);

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
        output.state.voice_volume[i]      = voices_[i].is_active() ? voices_[i].env_value() : 0.0f;
        output.state.voice_live_params[i] = voice_live_params_[i];
    }
}
