#include "RTPMidiConfig.h"
#include "RTPMidiConnection.h"

#if ENABLE_RTP_MIDI

#include "WifiProvisioning.h"
#include <AppleMIDI.h>
#include <WiFi.h>

#ifndef RTP_MIDI_SESSION_NAME
#define RTP_MIDI_SESSION_NAME "Piano BLE Bridge"
#endif

APPLEMIDI_CREATE_INSTANCE(WiFiUDP, NetworkMidi, RTP_MIDI_SESSION_NAME, DEFAULT_CONTROL_PORT);

namespace {

RTPMidiConnection* g_rtpOwner = nullptr;

void onRtpConnectedCb(const APPLEMIDI_NAMESPACE::ssrc_t& ssrc, const char* name)
{
    if (g_rtpOwner != nullptr) {
        g_rtpOwner->onRtpConnected(ssrc, name);
    }
}

void onRtpDisconnectedCb(const APPLEMIDI_NAMESPACE::ssrc_t& ssrc)
{
    if (g_rtpOwner != nullptr) {
        g_rtpOwner->onRtpDisconnected(ssrc);
    }
}

}  // namespace

void RTPMidiConnection::onRtpConnected(uint32_t ssrc, const char* name)
{
    rtpPeers_++;
    Serial.printf("[RTP] Session connected ssrc=%u name=%s\n", ssrc, name != nullptr ? name : "?");
}

void RTPMidiConnection::onRtpDisconnected(uint32_t ssrc)
{
    if (rtpPeers_ > 0) {
        rtpPeers_--;
    }
    Serial.printf("[RTP] Session disconnected ssrc=%u\n", ssrc);
}

#endif

bool RTPMidiConnection::begin(const char* sessionName)
{
#if !ENABLE_RTP_MIDI
    (void)sessionName;
    return false;
#else
    g_rtpOwner = this;
    wifiState_ = WifiState::kOff;
    ipString_[0] = '\0';
    rtpPeers_ = 0;
    rtpStarted_ = false;

    if (sessionName != nullptr && sessionName[0] != '\0') {
        AppleNetworkMidi.setName(sessionName);
    }

    AppleNetworkMidi.setHandleConnected(onRtpConnectedCb);
    AppleNetworkMidi.setHandleDisconnected(onRtpDisconnectedCb);

    wifiProvisioning.begin(sessionName);
    Serial.printf("[RTP] In Audio MIDI Setup, add device \"%s\" at port %u after WiFi connects\n",
                  sessionName != nullptr && sessionName[0] != '\0' ? sessionName : RTP_MIDI_SESSION_NAME,
                  AppleNetworkMidi.getPort());
    return true;
#endif
}

void RTPMidiConnection::task()
{
#if !ENABLE_RTP_MIDI
    return;
#else
    wifiProvisioning.task();

    if (wifiProvisioning.isConnected()) {
        if (!rtpStarted_) {
            wifiState_ = WifiState::kConnected;
            strncpy(ipString_, wifiProvisioning.localIpString(), sizeof(ipString_) - 1);
            ipString_[sizeof(ipString_) - 1] = '\0';
            NetworkMidi.begin();
            rtpStarted_ = true;
            Serial.printf("[RTP] WiFi ready %s\n", ipString_);
        }
        NetworkMidi.read();
        return;
    }

    if (wifiProvisioning.isSetupMode()) {
        wifiState_ = WifiState::kOff;
        rtpStarted_ = false;
        rtpPeers_ = 0;
        strncpy(ipString_, wifiProvisioning.localIpString(), sizeof(ipString_) - 1);
        ipString_[sizeof(ipString_) - 1] = '\0';
    }
#endif
}

bool RTPMidiConnection::isWifiConnected() const
{
#if !ENABLE_RTP_MIDI
    return false;
#else
    return wifiProvisioning.isConnected();
#endif
}

bool RTPMidiConnection::isWifiSetupMode() const
{
#if !ENABLE_RTP_MIDI
    return false;
#else
    return wifiProvisioning.isSetupMode();
#endif
}

const char* RTPMidiConnection::wifiSetupApName() const
{
#if !ENABLE_RTP_MIDI
    return "";
#else
    return wifiProvisioning.setupApSsid();
#endif
}

bool RTPMidiConnection::hasRtpSession() const
{
#if !ENABLE_RTP_MIDI
    return false;
#else
    return rtpPeers_ > 0;
#endif
}

const char* RTPMidiConnection::localIpString() const
{
#if !ENABLE_RTP_MIDI
    return "";
#else
    return ipString_;
#endif
}

void RTPMidiConnection::sendFromUsbPacket(const uint8_t* usbMidiPacket)
{
#if !ENABLE_RTP_MIDI
    (void)usbMidiPacket;
    return;
#else
    if (usbMidiPacket == nullptr || rtpPeers_ <= 0 || !wifiProvisioning.isConnected()) {
        return;
    }

    const uint8_t status = usbMidiPacket[1];
    if (status == 0x00 || status == 0xFE) {
        return;
    }

    if (status >= 0xF8) {
        NetworkMidi.sendRealTime(static_cast<MIDI_NAMESPACE::MidiType>(status));
        return;
    }

    const uint8_t channel = (status & 0x0F) + 1;
    switch (status & 0xF0) {
        case 0x80:
            NetworkMidi.sendNoteOff(usbMidiPacket[2], usbMidiPacket[3], channel);
            break;
        case 0x90:
            if (usbMidiPacket[3] == 0) {
                NetworkMidi.sendNoteOff(usbMidiPacket[2], 0, channel);
            } else {
                NetworkMidi.sendNoteOn(usbMidiPacket[2], usbMidiPacket[3], channel);
            }
            break;
        case 0xA0:
            NetworkMidi.sendAfterTouch(usbMidiPacket[2], usbMidiPacket[3], channel);
            break;
        case 0xB0:
            NetworkMidi.sendControlChange(usbMidiPacket[2], usbMidiPacket[3], channel);
            break;
        case 0xC0:
            NetworkMidi.sendProgramChange(usbMidiPacket[2], channel);
            break;
        case 0xD0:
            NetworkMidi.sendAfterTouch(usbMidiPacket[2], channel);
            break;
        case 0xE0: {
            const int bend = (static_cast<int>(usbMidiPacket[3]) << 7) | usbMidiPacket[2];
            NetworkMidi.sendPitchBend(bend, channel);
            break;
        }
        default:
            break;
    }
#endif
}
