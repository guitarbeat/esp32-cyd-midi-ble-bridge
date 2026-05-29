#include "MidiBridge.h"

#include "BLEConnection.h"
#include "BridgeSettings.h"
#include "BridgeUi.h"
#include "MidiCodec.h"

#if ENABLE_RTP_MIDI
#include "NetworkServices.h"
#endif

MidiBridge midiBridge;

void MidiBridge::begin(BLEConnection* ble, BridgeSettings* settings, BridgeUi* ui)
{
    ble_ = ble;
    settings_ = settings;
    ui_ = ui;
}

#if ENABLE_RTP_MIDI
void MidiBridge::setNetwork(NetworkServices* network)
{
    network_ = network;
}
#endif

MidiBridge::Result MidiBridge::forward(const uint8_t* data, size_t length, uint8_t outMidiPacket[4])
{
    if (data == nullptr || outMidiPacket == nullptr) {
        return Result::kIgnored;
    }

    counters_.usbPacketsSeen++;

    outMidiPacket[0] = data[0];
    outMidiPacket[1] = data[1];
    outMidiPacket[2] = data[2];
    outMidiPacket[3] = data[3];

    if (settings_ == nullptr || !settings_->transformUsbMidiPacket(outMidiPacket)) {
        return Result::kFiltered;
    }

    uint8_t blePacket[5] = {0};
    size_t bleLength = 0;

    if (!MidiCodec::buildBlePacket(outMidiPacket, length, static_cast<uint16_t>(millis()), blePacket, &bleLength)) {
        if (length >= 2) {
            Serial.printf("[USB] Ignored packet CIN=%02X status=%02X\n", outMidiPacket[0] & 0x0F, outMidiPacket[1]);
        } else {
            Serial.printf("[USB] Ignored short packet length=%u\n", static_cast<unsigned>(length));
        }
        return Result::kIgnored;
    }

    if (ble_ != nullptr) {
        Serial.printf("[USB] %-15s ch=%u data1=%3u data2=%3u BLE=%s\n",
                      MidiCodec::statusName(outMidiPacket[1]),
                      ((outMidiPacket[1] & 0x0F) + 1),
                      outMidiPacket[2],
                      outMidiPacket[3],
                      ble_->isConnected() ? "connected" : "waiting");
    }

    if (ui_ != nullptr && !ui_->isBridgePaused() && ble_ != nullptr) {
        const uint32_t t0 = micros();
        if (ble_->isConnected() && ble_->sendMidi(blePacket, bleLength)) {
            ble_->recordForwardLatency((micros() - t0) / 1000);
            counters_.blePacketsSent++;
        } else {
            counters_.blePacketsSkipped++;
        }

#if ENABLE_RTP_MIDI
        if (network_ != nullptr && network_->hasRtpSession()) {
            network_->forwardMidi(outMidiPacket);
            counters_.rtpPacketsSent++;
        }
#endif
    }

    return Result::kForwarded;
}
