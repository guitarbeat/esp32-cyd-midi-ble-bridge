#ifndef MIDI_BRIDGE_H
#define MIDI_BRIDGE_H

#include <Arduino.h>

#include "RTPMidiConfig.h"

class BLEConnection;
class BridgeSettings;
class BridgeUi;
class NetworkServices;

class MidiBridge {
public:
    struct Counters {
        uint32_t usbPacketsSeen = 0;
        uint32_t blePacketsSent = 0;
        uint32_t blePacketsSkipped = 0;
#if ENABLE_RTP_MIDI
        uint32_t rtpPacketsSent = 0;
#endif
    };

    enum class Result : uint8_t {
        kFiltered,
        kIgnored,
        kForwarded,
    };

    void begin(BLEConnection* ble, BridgeSettings* settings, BridgeUi* ui);
#if ENABLE_RTP_MIDI
    void setNetwork(NetworkServices* network);
#endif

    Result forward(const uint8_t* data, size_t length, uint8_t outMidiPacket[4]);

    const Counters& counters() const { return counters_; }

private:
    BLEConnection* ble_ = nullptr;
    BridgeSettings* settings_ = nullptr;
    BridgeUi* ui_ = nullptr;
#if ENABLE_RTP_MIDI
    NetworkServices* network_ = nullptr;
#endif
    Counters counters_;
};

extern MidiBridge midiBridge;

#endif
