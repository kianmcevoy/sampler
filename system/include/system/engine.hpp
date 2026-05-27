#ifndef SYSTEM_PROCESSOR_H
#define SYSTEM_PROCESSOR_H

#include "instrument/instrument.hpp"
#include "instrument/parameter_data.hpp"
#include "instrument/state_data.hpp"
#include "interface/parameter_interface.hpp"
#include "interface/state_interface.hpp"
#include "system/audio_data.hpp"
#include "system/control_builder.hpp"
#include "system/osc_control_data.hpp"
#include "system/osc_interface.hpp"
#include "interface/gui_data.hpp"
#include "instrument/audio_data.hpp"

#include "JuceHeader.h"

#include <array>

class EngineAudioProcessor  : public juce::AudioProcessor
                            #if JucePlugin_Enable_ARA
                             , public juce::AudioProcessorARAExtension
                            #endif
{
    public:

        EngineAudioProcessor();
        ~EngineAudioProcessor() override;

        //==============================================================================
        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;

        #ifndef JucePlugin_PreferredChannelConfigurations
            bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        #endif

        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;
        //==============================================================================
        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override;

        //==============================================================================
        const juce::String getName() const override;

        bool acceptsMidi() const override;
        bool producesMidi() const override;
        bool isMidiEffect() const override;
        double getTailLengthSeconds() const override;

        //==============================================================================
        int getNumPrograms() override;
        int getCurrentProgram() override;
        void setCurrentProgram (int index) override;
        const juce::String getProgramName (int index) override;
        void changeProgramName (int index, const juce::String& newName) override;

        //==============================================================================
        juce::Identifier get_xml_tag_for_autosave() const;
        juce::Identifier get_xml_tag_for_parameters() const;
        juce::Identifier get_xml_tag_for_settings() const;

        std::unique_ptr<juce::XmlElement> create_xml_for_autosave() const;
        std::unique_ptr<juce::XmlElement> create_xml_for_parameters() const;
        std::unique_ptr<juce::XmlElement> create_xml_for_settings() const;

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        virtual void save_parameters_to_xml(juce::XmlElement& xml);
        virtual void load_parameters_from_xml(const juce::XmlElement& xml);

        virtual void save_settings_to_xml(juce::XmlElement& xml);
        virtual void load_settings_from_xml(const juce::XmlElement& xml);

        void set_latency_compensation(int latency_in_samples);

        float& get_gui_scale_value_ref();

        std::vector<juce::RangedAudioParameter*> all_params;
        std::vector<juce::AudioParameterFloat*> float_params;
        std::vector<juce::AudioParameterBool*> bool_params;
        std::vector<juce::AudioParameterBool*> trigger_params;
        std::vector<juce::AudioParameterChoice*> choice_params;
        // Per-voice select buttons. Kept separate from bool_params so the
        // GUI can give them dedicated visual + click behaviour and so
        // ParameterInterface doesn't see them as ordinary controls. The
        // matching voice slot index lives in voice_button_indices.
        std::vector<juce::AudioParameterBool*> voice_button_params;
        std::vector<size_t> voice_button_indices;

        std::vector<juce::NormalisableRange<float>> float_param_ranges;

        OscInterface& get_osc_interface()
            { return this->osc_interface; }
        const OscInterface& get_osc_interface() const
            { return this->osc_interface; }

        GuiOutputData& get_gui_output_data()
            { return this->gui_output_data; }
        const GuiOutputData& get_gui_output_data() const
            { return this->gui_output_data; }

        GuiInputData& get_gui_input_data()
            { return this->gui_input_data; }
        const GuiInputData& get_gui_input_data() const
            { return this->gui_input_data; }

        std::array<SampleBuffer, max_layers>& get_layer_buffers()
            { return this->layer_buffers; }
        const std::array<SampleBuffer, max_layers>& get_layer_buffers() const
            { return this->layer_buffers; }

        const GuiControlBuilder& get_gui_control_builder() const
            { return this->gui_control_builder; }

    private:
        void safetyCheck(juce::AudioBuffer<float> &);

        void process_gui_controls();

        void process_osc_input(const juce::OSCMessage& message);
        void process_osc_output();

        std::unique_ptr<juce::AudioProcessorValueTreeState> processor_valuetree_state;

        idsp::RingBuffer<OscDataElement, 2048> osc_input_buffer;
        std::atomic_bool osc_output_pending;

        AudioData audio_data;
        GuiControlData gui_control_data;
        OscInputData osc_input_data;
        ParameterData parameter_data;
        StateData state_data;
        OscOutputData osc_output_data;
        UtilityData utility_data;
		std::array<SampleBuffer, max_layers> layer_buffers;
		GuiOutputData gui_output_data;
		GuiInputData gui_input_data;

        ParameterInterface parameter_interface;
        Instrument instrument;
        StateInterface state_interface;

        GuiControlBuilder gui_control_builder;

        float gui_scale_factor;

        OscInterface osc_interface;
};

#endif
