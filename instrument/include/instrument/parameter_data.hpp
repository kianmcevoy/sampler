#ifndef INSTRUMENT_PARAMETER_DATA_H
#define INSTRUMENT_PARAMETER_DATA_H

#include <cstddef>

/** Structure of parameter data for the instrument.
 * The data stored here should be hardware agnostic (as far as is practical) and
 * should be conditioned to the extent that the instrument may use it with no or
 * minimal additional processing.
 */
enum class Mode
{
	OneShot,
	Looping
};

struct ParameterData
{
	//playback controls
	float speed;
	float start;
	float length;
	float level;
	float pan;
	bool play;
	bool stop;
	bool loop;

	//envelope controls
	float time;
	float skew;
	float shape;
	bool loop_envelope;
    bool voice_stealing;
    bool envelope_sync;

	//random modulation
	float random_speed;
	float random_start;
	float random_length;
	float random_level;
	float random_pan;

    //envelope modulation
	float envelope_speed;
	float envelope_start;
	float envelope_length;
	float envelope_level;
	float envelope_pan;
};

#endif
