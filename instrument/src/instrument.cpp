#include "instrument/instrument.hpp"

#include <cmath>
#include <limits>

namespace
{
	// Equal-power pan: pan ∈ [0, 1] with 0=full left, 0.5=centre (≈ -3 dB on
	// each side), 1=full right. Avoids the -6 dB centre dip of linear pan.
	inline void compute_pan_gains(float pan, float& left, float& right)
	{
		left  = std::sqrt(1.0f - pan);
		right = std::sqrt(pan);
	}
}

Instrument::Instrument(InstrumentOutputData& output)
{

}

void Instrument::load(const InstrumentLoadData& loaded, InstrumentOutputData& output)
{

}

Instrument::Playhead& Instrument::acquire_playhead()
{
	for (auto& ph : playheads)
	{
		if (!ph.active) return ph;
	}
	// All slots busy — steal the oldest voice (smallest launch_seq).
	Playhead* victim = &playheads[0];
	uint64_t oldest = std::numeric_limits<uint64_t>::max();
	for (auto& ph : playheads)
	{
		if (ph.launch_seq < oldest)
		{
			oldest = ph.launch_seq;
			victim = &ph;
		}
	}
	return *victim;
}

void Instrument::process(const InstrumentInputData& input, InstrumentOutputData& output)
{
	const size_t block_size = output.audio.channel(0).size();
	const size_t buffer_size = input.buffer.loaded_sample.channel(0).size();
	if (buffer_size == 0)
	{
		return;
	}

	// --- play: single un-enveloped voice (gain = 1.0) ---
	if (input.parameter.play)
	{
		const size_t start_pos   = static_cast<size_t>(input.parameter.start * buffer_size);
		const size_t loop_length = static_cast<size_t>(input.parameter.length * buffer_size);
		const size_t end_pos     = idsp::min(start_pos + loop_length, buffer_size);

		Playhead& v = acquire_playhead();
		v.start_pos        = start_pos;
		v.end_pos          = end_pos;
		v.length           = loop_length;
		v.speed            = input.parameter.speed;
		v.level            = input.parameter.level;
		compute_pan_gains(input.parameter.pan, v.pan_left, v.pan_right);
		v.position         = (v.speed >= 0.0f) ? static_cast<float>(start_pos)
		                                       : static_cast<float>(end_pos - 1);
		v.active           = true;
		v.envelope_enabled = false;
		v.envelope.reset();
		v.launch_seq       = ++launch_counter;
	}

	// --- trigger: arm the envelope chain ---
	if (input.parameter.trigger)
	{
		chain_active            = true;
		chain_remaining         = input.parameter.repeats;
		chain_countdown_samples = 0; // fire on first sample of this block
	}

	chain_looping           = input.parameter.loop_envelope;

	// --- stop: kill every voice and cancel any running chain ---
	if (input.parameter.stop)
	{
		for (auto& ph : playheads) ph.active = false;
		chain_active = false;
	}

	// --- chain scheduler ---
	// Each iteration re-reads sliders, so every envelope in the chain
	// snapshots its own start / length / speed / time / skew / spacing
	// at its own launch moment — chain rhythm intentionally tracks the
	// jittered length/speed so random_* sliders shake the timing too.
	if (chain_active)
	{
		std::uniform_real_distribution<float> bipolar(-1.0f, 1.0f);

		while (chain_active && chain_countdown_samples <= 0)
		{
			// Per-launch random deviation. Bipolar additive offset scaled by
			// the random_X slider. random_speed multiplied by 8 to span the
			// full ±4 speed range (mirrors the speed mapping in
			// parameter_interface.cpp — keep these in sync).
			const float start_jitter  = bipolar(rng) * input.parameter.random_start;
			const float length_jitter = bipolar(rng) * input.parameter.random_length;
			const float speed_jitter  = bipolar(rng) * input.parameter.random_speed * 8.0f;
			const float level_jitter  = bipolar(rng) * input.parameter.random_level;
			const float pan_jitter    = bipolar(rng) * input.parameter.random_pan;

			const float jit_start  = idsp::clamp(input.parameter.start  + start_jitter,  0.0f, 1.0f);
			const float jit_length = idsp::clamp(input.parameter.length + length_jitter, 0.0f, 1.0f);
			const float jit_speed  = input.parameter.speed + speed_jitter;
			const float jit_level  = idsp::clamp(input.parameter.level + level_jitter, 0.0f, 1.0f);
			const float jit_pan    = idsp::clamp(input.parameter.pan + pan_jitter, 0.0f, 1.0f);

			const size_t start_pos   = static_cast<size_t>(jit_start * buffer_size);
			const size_t loop_length = idsp::max<size_t>(
				static_cast<size_t>(jit_length * buffer_size), 1);
			const size_t end_pos     = idsp::min(start_pos + loop_length, buffer_size);
			const size_t env_dur     = static_cast<size_t>(input.parameter.time * loop_length);
			const size_t attack      = static_cast<size_t>(input.parameter.skew * env_dur);
			const size_t release     = (env_dur > attack) ? (env_dur - attack) : 0;

			Playhead& v = acquire_playhead();
			v.start_pos        = start_pos;
			v.end_pos          = end_pos;
			v.length           = loop_length;
			v.speed            = jit_speed;
			v.level            = jit_level;
			compute_pan_gains(jit_pan, v.pan_left, v.pan_right);
			v.position         = (v.speed >= 0.0f) ? static_cast<float>(start_pos)
			                                       : static_cast<float>(end_pos - 1);
			v.active           = true;
			v.envelope_enabled = true;
			v.envelope.trigger(attack, release, input.parameter.shape);
			v.launch_seq       = ++launch_counter;

			// Spacing-derived offset to the next launch.
			//   spacing >= 0.5 → gap   = (spacing - 0.5) * 2 * loop_length
			//   spacing <  0.5 → overlap, next launch fires partway through this env.
			int64_t next_offset;
			const float spacing = input.parameter.spacing;
			if (spacing >= 0.5f)
			{
				next_offset = static_cast<int64_t>(env_dur)
					+ static_cast<int64_t>((spacing - 0.5f) * 2.0f * static_cast<float>(loop_length));
			}
			else
			{
				next_offset = static_cast<int64_t>(static_cast<float>(env_dur) * spacing * 2.0f);
			}
			if (next_offset < 1) next_offset = 1; // safety against zero-spacing runaway
			chain_countdown_samples += next_offset;

			if (!chain_looping)
			{
				if (chain_remaining > 0) --chain_remaining;
				if (chain_remaining == 0) chain_active = false;
			}
		}
		chain_countdown_samples -= static_cast<int64_t>(block_size);
	}

	// --- zero the output, then sum voice contributions ---
	for (size_t i = 0; i < block_size; ++i)
	{
		output.audio.channel(0)[i] = 0.0f;
		output.audio.channel(1)[i] = 0.0f;
	}

	const bool looping = input.parameter.loop;
	const auto& left_buffer  = input.buffer.sample[0];
	const auto& right_buffer = input.buffer.sample[1];

	for (auto& ph : playheads)
	{
		if (!ph.active) continue;

		for (size_t sample_idx = 0; sample_idx < block_size; ++sample_idx)
		{
			if (!check_bounds(ph, looping))
			{
				break; // out of bounds and not looping — voice deactivated
			}

			const Sample env_gain = ph.envelope_enabled ? ph.envelope.process() : 1.0f;
			const Sample voice_gain = env_gain * ph.level;
			output.audio.channel(0)[sample_idx] += voice_gain * ph.pan_left  * left_buffer.read_at(ph.position);
			output.audio.channel(1)[sample_idx] += voice_gain * ph.pan_right * right_buffer.read_at(ph.position);

			ph.position += ph.speed;
		}

		// Auto-free enveloped voices whose envelope has finished, so chain
		// launches don't permanently consume slots.
		if (ph.envelope_enabled && !ph.envelope.is_active())
		{
			ph.active = false;
		}
	}

	// --- publish display state ---
	output.state.playback_position = playheads[0].active ? playheads[0].position : -1.0f;
	for (size_t i = 0; i < max_playheads; ++i)
	{
		output.state.playhead_active[i]   = playheads[i].active;
		output.state.playhead_position[i] = playheads[i].position;
		// Enveloped voices show the envelope level as alpha (gold cursor fades
		// with the shape). Un-enveloped voices show full brightness.
		output.state.playhead_volume[i]   = playheads[i].active
			? (playheads[i].envelope_enabled ? playheads[i].envelope.current_value() : 1.0f)
			: 0.0f;
	}
}

bool Instrument::check_bounds(Playhead& ph, const bool looping)
{
	const float end_idx_float   = static_cast<float>(ph.end_pos);
	const float start_idx_float = static_cast<float>(ph.start_pos);
	const float duration_float  = static_cast<float>(ph.length);

	if (ph.position >= end_idx_float)
	{
		if (looping)
		{
			while (ph.position >= end_idx_float && duration_float > 0)
				ph.position -= duration_float;
			if (ph.position < start_idx_float)
				ph.position = start_idx_float;
		}
		else
		{
			ph.active = false;
			return false;
		}
	}
	else if (ph.position < start_idx_float)
	{
		if (looping)
		{
			while (ph.position < start_idx_float && duration_float > 0)
				ph.position += duration_float;
			if (ph.position >= end_idx_float)
				ph.position = end_idx_float - 1.0f;
		}
		else
		{
			ph.active = false;
			return false;
		}
	}

	return true;
}
