#ifndef SYSTEM_OSC_INTERFACE_H
#define SYSTEM_OSC_INTERFACE_H

#include "JuceHeader.h"

class OscInterface
{
    public:
        OscInterface();

        ~OscInterface();

        void set_output_port_number(int port_number);

        int get_output_port_number() const;

        void set_output_address(const juce::String& address);

        const juce::String& get_output_address() const;

        void set_output_address_and_port(const juce::String& address, int port_number);

        void send(const juce::OSCMessage& osc_message);

        void send(const juce::OSCBundle& osc_bundle);

        void set_input_port_number(int port_number);

        int get_input_port_number() const;

        void set_message_received_callback(std::function<void(const juce::OSCMessage&)> callback);

    private:
        void connect_output();
        void connect_input();

        void disconnect_output();
        void disconnect_input();

        void receive_message(const juce::OSCMessage& message);

        juce::OSCSender sender;

        juce::OSCReceiver receiver;
        class OscListener:
            public juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
        {
            public:
                OscListener(OscInterface& parent);

                void oscMessageReceived(const juce::OSCMessage& message) override;

                void oscBundleReceived(const juce::OSCBundle& bundle) override;

            private:
                OscInterface& parent;
        };
        OscListener listener;
        std::function<void(const juce::OSCMessage&)> rx_callback;

        juce::String output_address;
        int output_port;
        int input_port;
};

#endif
