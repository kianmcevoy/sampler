#include "system/engine.hpp"

#include "system/editor.hpp"
#include "system/gui_controls.hpp"

//==============================================================================
EngineAudioProcessor::EngineAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
                         ),
#endif
    processor_valuetree_state{nullptr},
    osc_input_buffer{},
    audio_data{
        .input = PolyDspBuffer{},
        .output = PolyDspBuffer{},
    },
    gui_control_data{},
    osc_input_data{
        .messages = {this->osc_input_buffer},
    },
    parameter_data{},
    state_data{},
    osc_output_data{},
    utility_data{},
	layer_buffers{},
	gui_output_data{},
	gui_input_data{},
    parameter_interface{[this](){
        ParameterInterfaceOutputData data {
            .parameter = this->parameter_data,
            .utility = this->utility_data,
			.gui = this->gui_output_data,
			.layer_buffers = this->layer_buffers,
        };
        return ParameterInterface(data);
    }()},
    instrument{[this](){
        InstrumentOutputData data {
            .audio = this->audio_data.output,
            .state = this->state_data,
        };
        return Instrument(data);
    }()},
    state_interface{[this](){
        StateInterfaceOutputData data {
            .osc = this->osc_output_data,
            .gui = this->gui_output_data,
        };
        return StateInterface(data);
    }()},
    gui_control_builder{},
    gui_scale_factor{1.f}
{
    jassert(juce::Identifier::isValidIdentifier(this->get_xml_tag_for_autosave().toString()));

    build_gui_control_scheme(this->gui_control_builder);
    this->processor_valuetree_state = std::make_unique<juce::AudioProcessorValueTreeState>(
        *this,
        nullptr,
        "Parameters",
        this->gui_control_builder.transfer_parameter_layout()
    );
    for (const auto& id : this->gui_control_builder.get_all_parameter_identifiers())
    {
        const auto param = this->processor_valuetree_state->getParameter(id);
        jassert(param != nullptr);
        this->all_params.push_back(param);
    }
    for (const auto& id : this->gui_control_builder.get_slider_identifiers())
    {
        const auto param_raw = this->processor_valuetree_state->getParameter(id);
        jassert(dynamic_cast<juce::AudioParameterFloat*>(param_raw) != nullptr);
        const auto param = static_cast<juce::AudioParameterFloat*>(param_raw);
        this->float_params.push_back(param);
        const auto& builder_ranges = this->gui_control_builder.get_slider_ranges();
        const size_t slider_idx = this->float_params.size() - 1;
        this->float_param_ranges.emplace_back(slider_idx < builder_ranges.size()
            ? builder_ranges[slider_idx]
            : juce::NormalisableRange<float>(0.0f, 1.0f));
        this->gui_control_data.sliders.insert(std::pair<juce::String, float>{id, 0.f});
    }
    for (const auto& id : this->gui_control_builder.get_button_identifiers())
    {
        const auto param_raw = this->processor_valuetree_state->getParameter(id);
        jassert(dynamic_cast<juce::AudioParameterBool*>(param_raw) != nullptr);
        const auto param = static_cast<juce::AudioParameterBool*>(param_raw);
        this->bool_params.push_back(param);
        this->gui_control_data.buttons.insert(std::pair<juce::String, bool>{id, false});
    }
    {
        const auto& voice_ids = this->gui_control_builder.get_voice_button_identifiers();
        const auto& voice_indices = this->gui_control_builder.get_voice_button_indices();
        jassert(voice_ids.size() == voice_indices.size());
        for (size_t i = 0; i < voice_ids.size(); ++i)
        {
            const auto& id = voice_ids[i];
            const auto param_raw = this->processor_valuetree_state->getParameter(id);
            jassert(dynamic_cast<juce::AudioParameterBool*>(param_raw) != nullptr);
            const auto param = static_cast<juce::AudioParameterBool*>(param_raw);
            this->voice_button_params.push_back(param);
            this->voice_button_indices.push_back(voice_indices[i]);
        }
    }
    for (const auto& id : this->gui_control_builder.get_trigger_identifiers())
    {
        const auto param_raw = this->processor_valuetree_state->getParameter(id);
        jassert(dynamic_cast<juce::AudioParameterBool*>(param_raw) != nullptr);
        const auto param = static_cast<juce::AudioParameterBool*>(param_raw);
        this->trigger_params.push_back(param);
        this->gui_control_data.triggers.insert(std::pair<juce::String, bool>{id, false});
    }
    for (const auto& id : this->gui_control_builder.get_dropdown_identifiers())
    {
        const auto param_raw = this->processor_valuetree_state->getParameter(id);
        jassert(dynamic_cast<juce::AudioParameterChoice*>(param_raw) != nullptr);
        const auto param = static_cast<juce::AudioParameterChoice*>(param_raw);
        this->choice_params.push_back(param);
        this->gui_control_data.dropdowns.insert(std::pair<juce::String, size_t>{id, 0});
    }

    this->osc_interface.set_message_received_callback([this](const juce::OSCMessage& message)
    {
        this->process_osc_input(message);
    });
    this->osc_interface.set_input_port_number(11046);
}

EngineAudioProcessor::~EngineAudioProcessor()
{

}

//==============================================================================
const juce::String EngineAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool EngineAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool EngineAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool EngineAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double EngineAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int EngineAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int EngineAudioProcessor::getCurrentProgram()
{
    return 0;
}

void EngineAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String EngineAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void EngineAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void EngineAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    this->audio_data.input.resize(samplesPerBlock);
    this->audio_data.output.resize(samplesPerBlock);
    this->instrument.prepare(sampleRate);
}

void EngineAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool EngineAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif
    return true;
  #endif

}
#endif

void EngineAudioProcessor::safetyCheck(juce::AudioBuffer<float>& buffer)
{
    //safety check to make sure
    jassert(this->audio_data.input.size() >= buffer.getNumChannels());
    jassert(this->audio_data.output.size() <= buffer.getNumChannels());

    this->audio_data.input.resize(buffer.getNumSamples());
    this->audio_data.output.resize(buffer.getNumSamples());
}

void EngineAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    this->safetyCheck(buffer);

    for(size_t ch = 0; ch < this->audio_data.input.size(); ch++)
    {
        auto in = buffer.getReadPointer(ch);
        for(size_t i = 0; i < this->audio_data.input[ch].size(); i++)
            this->audio_data.input[ch][i] = in[i];
    }

    {
        this->process_gui_controls();

        const ParameterInterfaceInputData input
        {
            .controls = this->gui_control_data,
            .osc = this->osc_input_data,
            .state = this->state_data,
			.gui = this->gui_input_data,
			.midi = midiMessages,
        };
        ParameterInterfaceOutputData output
        {
            .parameter = this->parameter_data,
            .utility = this->utility_data,
            .gui = this->gui_output_data,
            .layer_buffers = this->layer_buffers,
        };
        this->parameter_interface.process(input, output);
    }

    {
        InstrumentInputData input
        {
            .audio = this->audio_data.input,
            .parameter = this->parameter_data,
            .layer_buffers = this->layer_buffers,
        };
        InstrumentOutputData output
        {
            .audio = this->audio_data.output,
            .state = this->state_data,
        };
        this->instrument.process(input, output);
    }

    {
        StateInterfaceInputData input
        {
            .state = this->state_data,
            .utility = this->utility_data,
        };
        StateInterfaceOutputData output
        {
            .osc = this->osc_output_data,
            .gui = this->gui_output_data,
        };
        this->state_interface.process(input, output);
    }

    this->process_osc_output();

    for (auto& param : this->trigger_params)
    {
        param->setValueNotifyingHost(param->convertTo0to1(false));
    }

    for(size_t ch = 0; ch < this->audio_data.output.size(); ch++)
    {
        auto out = buffer.getWritePointer(ch);
        for(size_t i = 0; i < this->audio_data.output[ch].size(); i++)
           out[i] = this->audio_data.output[ch][i];
    }
}

void EngineAudioProcessor::process_gui_controls()
{
    for (size_t i = 0; i < this->float_params.size(); i++)
    {
        const auto* param = this->float_params[i];
        const auto& range = this->float_param_ranges[i];
        const auto id = param->getParameterID();
        float& value = this->gui_control_data.sliders.at(id);
        value = range.convertFrom0to1(param->get());
    }
    for (size_t i = 0; i < this->bool_params.size(); i++)
    {
        const auto* param = this->bool_params[i];
        const auto id = param->getParameterID();
        bool& value = this->gui_control_data.buttons.at(id);
        value = param->get();
    }
    for (size_t i = 0; i < this->trigger_params.size(); i++)
    {
        const auto* param = this->trigger_params[i];
        const auto id = param->getParameterID();
        bool& value = this->gui_control_data.triggers.at(id);
        value = param->get();
    }
    for (size_t i = 0; i < this->choice_params.size(); i++)
    {
        const auto* param = this->choice_params[i];
        const auto id = param->getParameterID();
        size_t& value = this->gui_control_data.dropdowns.at(id);
        value = param->getIndex();
    }
}

void EngineAudioProcessor::process_osc_input(const juce::OSCMessage& message)
{
    if (message.getAddressPattern().containsWildcards())
    {
        return;
    }

    const auto id = message.getAddressPattern().toString().trimCharactersAtStart("/");
    const auto* param = this->processor_valuetree_state->getParameter(id);
    if (param == nullptr)
    {
        return;
    }

    for (const auto& arg : message)
    {
        const OscDataElement data(param->getParameterID(), arg);
        this->osc_input_buffer.write(data);
    }
}

void EngineAudioProcessor::process_osc_output()
{
    if ((this->osc_output_data.messages.data_available() > 0) && !this->osc_output_pending)
    {
        this->osc_output_pending = true;
        juce::MessageManager::callAsync([this]()
        {
            while (this->osc_output_data.messages.data_available() > 0)
            {
                const auto element = this->osc_output_data.messages.read();
                auto address = element.id;
                jassert(!address.isEmpty());
                if (!address.startsWith("/"))
                {
                    address = "/" + address;
                }
                const juce::OSCMessage message(address, element.value);
                this->osc_interface.send(message);
            }
            this->osc_output_pending = false;
        });
    }
}

//==============================================================================
bool EngineAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* EngineAudioProcessor::createEditor()
{
    try
    {
        const auto start = std::chrono::high_resolution_clock::now();
        const auto self = dynamic_cast<EngineAudioProcessor*>(this);
        jassert(self != nullptr);
        auto editor = new EngineAudioProcessorEditor(*self);
        const auto end = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        DBG("Editor took " << duration.count() << "ms to construct");
        return editor;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Couldn't create editor! Error was: " << e.what() << std::endl;
    }

    return nullptr;
}

juce::Identifier EngineAudioProcessor::get_xml_tag_for_autosave() const
{
    jassert(juce::Identifier::isValidIdentifier(juce::String(JucePlugin_Name).removeCharacters(" ") + "Data"));
    return juce::String(JucePlugin_Name).removeCharacters(" ") + "Data";
}

juce::Identifier EngineAudioProcessor::get_xml_tag_for_parameters() const
{
    return this->processor_valuetree_state->state.getType();
}

juce::Identifier EngineAudioProcessor::get_xml_tag_for_settings() const
{
    jassert(juce::Identifier::isValidIdentifier("Settings"));
    return "Settings";
}

std::unique_ptr<juce::XmlElement> EngineAudioProcessor::create_xml_for_autosave() const
{
    auto xml = std::make_unique<juce::XmlElement>(this->get_xml_tag_for_autosave());
    jassert(xml != nullptr);
    return xml;
}

std::unique_ptr<juce::XmlElement> EngineAudioProcessor::create_xml_for_parameters() const
{
    auto xml = std::make_unique<juce::XmlElement>(this->get_xml_tag_for_parameters());
    jassert(xml != nullptr);
    return xml;
}

std::unique_ptr<juce::XmlElement> EngineAudioProcessor::create_xml_for_settings() const
{
    auto xml = std::make_unique<juce::XmlElement>(this->get_xml_tag_for_settings());
    jassert(xml != nullptr);
    return xml;
}

void EngineAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto xml = this->create_xml_for_autosave();

    auto parameter_xml = this->create_xml_for_parameters();
    this->save_parameters_to_xml(*parameter_xml);
    xml->addChildElement(parameter_xml.release());

    auto settings_xml = this->create_xml_for_settings();
    this->save_settings_to_xml(*settings_xml);
    xml->addChildElement(settings_xml.release());

    copyXmlToBinary(*xml, destData);
}

void EngineAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
    {
        DBG("Failed to read autosave data");
        return;
    }
    if (!xml->hasTagName(this->get_xml_tag_for_autosave()))
    {
        DBG("Failed to autoload any data");
        return;
    }

    const auto* const parameter_xml = xml->getChildByName(this->get_xml_tag_for_parameters());
    if (parameter_xml == nullptr)
    {
        DBG("Failed to autoload parameter data");
        return;
    }
    this->load_parameters_from_xml(*parameter_xml);

    const auto* const settings_xml = xml->getChildByName(this->get_xml_tag_for_settings());
    if (settings_xml == nullptr)
    {
        DBG("Failed to autoload settings data");
        return;
    }
    this->load_settings_from_xml(*settings_xml);
}

void EngineAudioProcessor::save_parameters_to_xml(juce::XmlElement& xml)
{
    for (const auto param : this->all_params)
    {
        auto param_xml = xml.createNewChildElement("PARAM");
        param_xml->setAttribute("id", param->getParameterID());
        param_xml->setAttribute("value", param->convertFrom0to1(param->getValue()));
    }
}

void EngineAudioProcessor::load_parameters_from_xml(const juce::XmlElement& xml)
{
    for (auto param : this->all_params)
    {
        const auto param_xml = xml.getChildByAttribute("id", param->getParameterID());
        if (param_xml != nullptr)
        {
            const auto default_value = param->convertFrom0to1(param->getDefaultValue());
            const auto value = param_xml->getDoubleAttribute("value", default_value);
            DBG("Autoloaded parameter " << param->getName(64) << " value: " << value);
            param->setValueNotifyingHost(param->convertTo0to1(value));
        }
        else
        {
            DBG("Failed to autoload parameter " << param->getName(64) << ", defaulting to: " << param->convertFrom0to1(param->getDefaultValue()));
            param->setValueNotifyingHost(param->getDefaultValue());
        }
    }
}

void EngineAudioProcessor::save_settings_to_xml(juce::XmlElement& xml)
{
    while (auto* existing = xml.getChildByName("GuiScale"))
        xml.removeChildElement(existing, true);
    const auto gui_scale_element = xml.createNewChildElement("GuiScale");
    jassert(gui_scale_element != nullptr);
    gui_scale_element->setAttribute("scale", this->gui_scale_factor);

    jassert(this->float_params.size() == this->float_param_ranges.size());
    for (size_t i = 0; i < this->float_params.size(); i++)
    {
        auto param_range_xml = xml.createNewChildElement("ParamRange");
        param_range_xml->setAttribute("id", this->float_params[i]->getParameterID());
        param_range_xml->setAttribute("min", this->float_param_ranges[i].start);
        param_range_xml->setAttribute("max", this->float_param_ranges[i].end);
    }
}

void EngineAudioProcessor::load_settings_from_xml(const juce::XmlElement& xml)
{
    // Load window scaling
    if (const auto* const gui_scale_element = xml.getChildByName("GuiScale"))
    {
        this->gui_scale_factor = gui_scale_element->getDoubleAttribute("scale", 1.0);
    }
    else
    {
        this->gui_scale_factor = 1.f;
    }

    jassert(this->float_params.size() == this->float_param_ranges.size());
    for (size_t i = 0; i < this->float_params.size(); i++)
    {
        const auto param_xml = xml.getChildByAttribute("id", this->float_params[i]->getParameterID());
        if (param_xml != nullptr && param_xml->hasAttribute("min") && param_xml->hasAttribute("max"))
        {
            // Only override the slider's declared range when the autosave
            // actually contains explicit min/max attributes — otherwise
            // getDoubleAttribute would fall back to its (0, 1) defaults and
            // silently clobber the ranges set in controls.cpp.
            const auto min = param_xml->getDoubleAttribute("min");
            const auto max = param_xml->getDoubleAttribute("max");
            DBG("Autoloaded parameter " << this->float_params[i]->getName(64) << " range: [" << min << ":" << max << "]");
            this->float_param_ranges[i].start = min;
            this->float_param_ranges[i].end = max;
        }
        else
        {
            DBG("Keeping declared range for parameter " << this->float_params[i]->getName(64));
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    try
    {
        const auto start = std::chrono::high_resolution_clock::now();
        auto engine = new EngineAudioProcessor();
        const auto end = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        DBG("Engine took " << duration.count() << "ms to construct");
        return engine;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Couldn't create engine! Error was: " << e.what() << std::endl;
    }

    return nullptr;
}

void EngineAudioProcessor::set_latency_compensation(int latency)
{
    this->setLatencySamples(latency);
}

float& EngineAudioProcessor::get_gui_scale_value_ref()
{
    return this->gui_scale_factor;
}
