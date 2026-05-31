#ifndef UART_CONNECTION_H
#define UART_CONNECTION_H

#include "Transport.h"
#include "MidiCodec.h"
#include <HardwareSerial.h>

/**
 * @brief MIDI-over-UART Transport (DIN MIDI).
 * 
 * Deep Module Principle: Encapsulates HardwareSerial and uses MidiCodec::Parser
 * to handle Running Status and SysEx streams, providing clean MIDI packets to the bridge.
 */
class UartConnection : public Transport {
public:
    UartConnection(HardwareSerial& serial = Serial2, int rxPin = 15, int txPin = 16);
    
    bool begin();
    const char* name() const override { return "UART-MIDI"; }
    bool isConnected() const override { return true; } // Always ready once initialized
    bool sendMidi(const uint8_t* packet, size_t length) override;
    void task() override;

private:
    static void onMidiReceived(uint8_t status, const uint8_t* data, size_t length, void* arg);

    HardwareSerial& serial_;
    int rxPin_;
    int txPin_;
    MidiCodec::Parser parser_;
    bool initialized_;
};

#endif
