#ifndef PARAMETER_INTERFACE_H
#define PARAMETER_INTERFACE_H

#include "system/parameter_interface_data.hpp"

/** This class is used for translating hardware controls and other means of
 * control input (e.g. MIDI) into parameters used by the Instrument.
 */
class ParameterInterface
{
    public:
        /** Use the constructor to initialise any interface-internal data, and
         * to initialise parameter and utility data.
         */
        ParameterInterface(ParameterInterfaceOutputData& output);
        ~ParameterInterface() = default;

        /** This method is called on startup, once any autosaved data has been
         * loaded.
         * @note This method is not guaranteed to be called on startup, so
         * should not be relied upon for data initialisation.
         */
        void load(const ParameterInterfaceLoadData& loaded, ParameterInterfaceOutputData& output);

        /** DSP loop processor.
         * Use this to translate control data into instrument parameters. This
         * includes UI features, such as button combinations.
         */
        void process(const ParameterInterfaceInputData& input, ParameterInterfaceOutputData& output);

    private:
        bool load_sample_into_buffer(const juce::File& audio_file, ParameterInterfaceOutputData& output, float start_slider, float length_slider);
};

#endif
