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

    Voice::TriggerParams build_trigger_params(const ParameterData& p,
                                              size_t               buffer_size,
                                              float                sample_rate,
                                              uint32_t&            rng_state)
    {
        // Per-launch random deviation (bipolar offset scaled by random_X slider).
        // random_speed multiplied by 8 to span the full ±4 speed range.
        const float jit_start  = idsp::clamp(p.start  + bipolar_rand(rng_state) * p.random_start,  0.f, 1.f);
        const float jit_length = idsp::clamp(p.length + bipolar_rand(rng_state) * p.random_length, 0.f, 1.f);
        const float jit_speed  = p.speed + bipolar_rand(rng_state) * p.random_speed * 8.f;
        const float jit_level  = idsp::clamp(p.level  + bipolar_rand(rng_state) * p.random_level,  0.f, 1.f);
        const float jit_pan    = idsp::clamp(p.pan    + bipolar_rand(rng_state) * p.random_pan,    0.f, 1.f);

        const size_t start_pos = static_cast<size_t>(jit_start * static_cast<float>(buffer_size));
        const size_t loop_len  = idsp::max<size_t>(
            static_cast<size_t>(jit_length * static_cast<float>(buffer_size)), 1);
        const size_t end_pos   = idsp::min(start_pos + loop_len, buffer_size);

        // Envelope duration: sync ON ⇒ fraction of loop length; sync OFF ⇒ 0..5 s.
        size_t env_dur = p.envelope_sync
            ? static_cast<size_t>(p.time * static_cast<float>(loop_len))
            : static_cast<size_t>(p.time * 5.0f * sample_rate);
        if (env_dur == 0) env_dur = 1; // guard: a 0-length envelope would idle on the first sample

        const size_t attack  = static_cast<size_t>(p.skew * static_cast<float>(env_dur));
        const size_t release = (env_dur > attack) ? (env_dur - attack) : 0;

        return Voice::TriggerParams{
            .start_pos    = start_pos,
            .end_pos      = end_pos,
            .length       = loop_len,
            .base_speed   = jit_speed,
            .base_level   = jit_level,
            .base_pan     = jit_pan,
            .sample_loops = p.loop,
            .env_attack   = attack,
            .env_release  = release,
            .env_shape    = p.shape,
            .env_loops    = p.loop_envelope,
            .env_sync     = p.envelope_sync,
            .depth_speed  = p.envelope_speed,
            .depth_start  = p.envelope_start,
            .depth_length = p.envelope_length,
            .depth_level  = p.envelope_level,
            .depth_pan    = p.envelope_pan,
        };
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

    // --- play: allocate a voice and trigger it ---
    if (p.play)
    {
        if (Voice* v = allocator_.acquire(voices_, p.voice_stealing))
        {
            v->trigger(build_trigger_params(p, buffer_size, sample_rate_, rng_state_),
                       ++launch_counter_);
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

    for (auto& v : voices_)
    {
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
        output.state.voice_active[i]   = voices_[i].is_active();
        output.state.voice_position[i] = voices_[i].position();
        output.state.voice_volume[i]   = voices_[i].is_active() ? voices_[i].env_value() : 0.0f;
    }
}
