#include "UartConnection.h"

UartConnection::UartConnection(HardwareSerial& serial, int rxPin, int txPin)
    : serial_(serial), rxPin_(rxPin), txPin_(txPin), parser_(onMidiReceived, this), initialized_(false)
{
}

bool UartConnection::begin() {
    // 31250 is the standard MIDI baud rate
    serial_.begin(31250, SERIAL_8N1, rxPin_, txPin_);
    initialized_ = true;
    return true;
}

bool UartConnection::sendMidi(const uint8_t* packet, size_t length) {
    if (!initialized_ || packet == nullptr || length == 0) return false;
    serial_.write(packet, length);
    return true;
}

void UartConnection::task() {
    if (!initialized_) return;
    
    // Read available bytes and feed the parser
    // Deep Module: The parser handles all state (running status, sysex)
    int count = 0;
    while (serial_.available() && count < 64) { // Limit burst to prevent blocking
        parser_.parse(serial_.read());
        count++;
    }
}

void UartConnection::onMidiReceived(uint8_t status, const uint8_t* data, size_t length, void* arg) {
    UartConnection* self = static_cast<UartConnection*>(arg);
    if (!self || !self->receiveCallback_) return;

    // Pack into a temporary buffer to present a complete message to the bridge
    uint8_t packet[256]; // Sufficient for standard messages and SysEx fragments
    if (length + 1 > sizeof(packet)) return;

    packet[0] = status;
    if (length > 0 && data != nullptr) {
        memcpy(packet + 1, data, length);
    }
    
    self->receiveCallback_(packet, 1 + length);
}
