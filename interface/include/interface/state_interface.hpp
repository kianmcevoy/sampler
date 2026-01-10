#ifndef STATE_INTERFACE_H
#define STATE_INTERFACE_H

#include "system/state_interface_data.hpp"

/** This class is used for processing the instrument's state, and informing the
 * Display of what to show.
 */
class StateInterface
{
    public:
        /** Use the constructor to initialise any interface-internal data, and
         * to set initial display data.
         */
        StateInterface(StateInterfaceOutputData& output);
        ~StateInterface() = default;

        /** This method is called on startup, once any autosaved data has been
         * loaded.
         * @note This method is not guaranteed to be called on startup, so
         * should not be relied upon for data initialisation.
         */
        void load(const StateInterfaceLoadData& loaded, StateInterfaceOutputData& output);

        /** DSP loop processor.
         * Use this to determine what the display should show based on the state
         * of the instrument and any utility data.
         */
        void process(const StateInterfaceInputData& input, StateInterfaceOutputData& output);
};

#endif
