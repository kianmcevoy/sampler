#include "system/osc_interface.hpp"
#include "instrument/instrument.hpp"
#include "idsp/functions.hpp"

static inline void print_errno()
{
    switch (errno)
    {
        case EACCES: DBG("Error was: EACCES"); break;
        case EAGAIN: DBG("Error was: EAGAIN"); break;
        case EBADF: DBG("Error was: EBADF"); break;
        case ECONNRESET: DBG("Error was: ECONNRESET"); break;
        case EDESTADDRREQ: DBG("Error was: EDESTADDRREQ"); break;
        case EFAULT: DBG("Error was: EFAULT"); break;
        case EINTR: DBG("Error was: EINTR"); break;
        case EINVAL: DBG("Error was: EINVAL"); break;
        case EISCONN: DBG("Error was: EISCONN"); break;
        case EMSGSIZE: DBG("Error was: EMSGSIZE"); break;
        case ENOBUFS: DBG("Error was: ENOBUFS"); break;
        case ENOMEM: DBG("Error was: ENOMEM"); break;
        case ENOTCONN: DBG("Error was: ENOTCONN"); break;
        case ENOTSOCK: DBG("Error was: ENOTSOCK"); break;
        case EOPNOTSUPP: DBG("Error was: EOPNOTSUPP"); break;
        case EPIPE: DBG("Error was: EPIPE"); break;
        default: DBG("Error code: " << errno); break;
    }
}

static constexpr void assert_valid_udp_port(int port)
{
    jassert(idsp::is_between(port, 0, 65535));
}

OscInterface::OscInterface():
sender{},
receiver{},
listener(*this),
rx_callback{},
output_address{"127.0.0.1"},
output_port{11045},
input_port{11046}
{
    this->connect_output();
}

OscInterface::~OscInterface()
{
    this->disconnect_output();
    this->disconnect_input();
    this->receiver.removeListener(&this->listener);
}

void OscInterface::set_output_port_number(int port_number)
{
    assert_valid_udp_port(port_number);
    this->output_port = port_number;
    this->connect_output();
}

int OscInterface::get_output_port_number() const
{
    return this->output_port;
}

void OscInterface::set_output_address(const juce::String& address)
{
    this->output_address = address;
    this->connect_output();
}

const juce::String& OscInterface::get_output_address() const
{
    return this->output_address;
}

void OscInterface::set_output_address_and_port(const juce::String& address, int port_number)
{
    this->output_address = address;
    this->output_port = port_number;
    this->connect_output();
}

void OscInterface::send(const juce::OSCMessage& message)
{
    const bool success = this->sender.send(message);
    if (!success)
    {
        DBG("Could not send OSC message");
        print_errno();
    }
}

void OscInterface::send(const juce::OSCBundle& bundle)
{
    const bool success = this->sender.send(bundle);
    if (!success)
    {
        DBG("Could not send OSC bundle");
        print_errno();
    }
}

void OscInterface::set_input_port_number(int port_number)
{
    assert_valid_udp_port(port_number);
    this->input_port = port_number;
    this->connect_input();
}

int OscInterface::get_input_port_number() const
{
    return this->input_port;
}

void OscInterface::set_message_received_callback(std::function<void(const juce::OSCMessage&)> callback)
{
    this->rx_callback = callback;

    if (callback)
    {
        this->receiver.addListener(&this->listener);
    }
    else
    {
        this->receiver.removeListener(&this->listener);
    }
}

void OscInterface::connect_output()
{
    const bool success = this->sender.connect(this->output_address, this->output_port);
    if (!success)
    {
        DBG("Error: could not connect to UDP host " << this->output_address << " on port " << this->output_port);
        print_errno();
    }
}

void OscInterface::connect_input()
{
    const bool success = this->receiver.connect(this->input_port);
    if (!success)
    {
        DBG("Error: could not connect to UDP socket on port " << this->input_port);
        print_errno();
    }
}

void OscInterface::disconnect_output()
{
    const bool success = this->sender.disconnect();
    if (!success)
    {
        DBG("Error: could not disconnect from UDP host " << this->output_address << " on port " << this->output_port);
        print_errno();
    }
}

void OscInterface::disconnect_input()
{
    const bool success = this->receiver.disconnect();
    if (!success)
    {
        DBG("Error: could not disconnect from UDP socket on port " << this->output_port);
        print_errno();
    }
}

void OscInterface::receive_message(const juce::OSCMessage& message)
{
    // Might still need this check as we're dealing with message thread,
    // callback could possibly occur after listener removal?
    if (this->rx_callback)
    {
        std::invoke(this->rx_callback, message);
    }
}

OscInterface::OscListener::OscListener(OscInterface& p):
parent{p}
{}

void OscInterface::OscListener::oscMessageReceived(const juce::OSCMessage& message)
{
    this->parent.receive_message(message);
}

void OscInterface::OscListener::oscBundleReceived(const juce::OSCBundle& bundle)
{
    for (const auto& element : bundle)
    {
        if (element.isMessage())
        {
            this->oscMessageReceived(element.getMessage());
        }
        else
        {
            this->oscBundleReceived(element.getBundle());
        }
    }
}
