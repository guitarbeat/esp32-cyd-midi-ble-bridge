#ifndef NETWORK_SERVICES_H
#define NETWORK_SERVICES_H

#include <Arduino.h>

#include "RTPMidiConfig.h"

class RTPMidiConnection;

// WiFi provisioning, Apple RTP-MIDI, and OTA — one orchestration module for the sketch.
class NetworkServices {
public:
    bool begin(const char* sessionName, const char* otaHostname);
    void task();

#if ENABLE_RTP_MIDI
    void enterSetupMode();
    bool isLanReady() const;
    bool isSetupMode() const;
    const char* setupApSsid() const;
    const char* localIpString() const;
    bool hasRtpSession() const;
    void forwardMidi(const uint8_t* usbMidiPacket);
#endif

#if ENABLE_OTA
    bool isOtaActive() const;
#endif

private:
#if ENABLE_RTP_MIDI
    RTPMidiConnection* rtp_ = nullptr;
#endif
};

extern NetworkServices networkServices;

#endif
