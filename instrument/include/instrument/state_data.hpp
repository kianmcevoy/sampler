#ifndef INSTRUMENT_STATE_DATA_H
#define INSTRUMENT_STATE_DATA_H

/** Structure of output/state data for the instrument.
 * Use this to communicate the state of the instrument to the output, i.e. the
 * display processor.
 * Try to avoid thinking about the instrument's state as the same thing as
 * its display state. While the two may be heavily intertwined in the project's
 * current form, this may not be the case if you use this instrument in another
 * project with a different form of UI.
 */
struct StateData
{

};

#endif
