#ifndef RTP_MIDI_CONNECTION_H
#define RTP_MIDI_CONNECTION_H

#include <Arduino.h>

#ifndef ENABLE_RTP_MIDI
#define ENABLE_RTP_MIDI 0
#endif

class RTPMidiConnection {
public:
    bool begin(const char* sessionName);
    void task();

    bool isWifiConnected() const;
    bool isWifiSetupMode() const;
    const char* wifiSetupApName() const;
    bool hasRtpSession() const;
    const char* localIpString() const;

    /** @brief Sends a raw MIDI message to all connected RTP peers. */
    void sendRawMidi(const uint8_t* midiPacket, size_t length);

#if ENABLE_RTP_MIDI
    void onRtpConnected(uint32_t ssrc, const char* name);
    void onRtpDisconnected(uint32_t ssrc);
#endif

private:
#if ENABLE_RTP_MIDI
    enum class WifiState : uint8_t { kOff, kConnecting, kConnected };

    WifiState wifiState_ = WifiState::kOff;
    char ipString_[16] = {0};
    int8_t rtpPeers_ = 0;
    bool rtpStarted_ = false;
#endif
};

#endif
