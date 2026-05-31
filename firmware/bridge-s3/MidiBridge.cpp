#include "MidiBridge.h"

#include "BridgeSystem.h"
#include "BridgeUi.h"
#include "MidiCodec.h"
#include "MidiEngine.h"

#if ENABLE_RTP_MIDI
#include "ConnectivityManager.h"
#endif

MidiBridge midiBridge;

void MidiBridge::begin(BridgeSettings* settings, BridgeUi* ui)
{
    settings_ = settings;
    ui_ = ui;
}

void MidiBridge::setMidiEngine(MidiEngine* engine)
{
    engine_ = engine;
}

void MidiBridge::addTransport(Transport* transport)
{
    if (transport == nullptr) return;
    transports_.push_back(transport);
    transport->setReceiveCallback([this, transport](const uint8_t* data, size_t length) {
        this->onMidiReceived(transport, data, length);
    });
}

MidiBridge::Result MidiBridge::route(Transport* source, const uint8_t* data, size_t length)
{
    if (data == nullptr || length < 1) return Result::kIgnored;

    // Standardize to 3-byte Raw MIDI for processing
    // (If source is USB, 'data' is already raw MIDI because we extract it in USBConnection)
    uint8_t rawMidi[3] = {0, 0, 0};
    memcpy(rawMidi, data, length > 3 ? 3 : length);

    // 1. Live Processing (MidiEngine)
    if (engine_ != nullptr) {
        // processPacket works on the actual buffer to allow in-place transformation (like transpose)
        if (!engine_->processPacket(rawMidi, 3)) {
            return Result::kFiltered;
        }
    }

    // 2. UI Notification (Observing the transformed state)
    if (ui_ != nullptr) {
        // UI uses 4-byte USB-style for its internal log, we convert back for the shim
        uint8_t uiPacket[4] = {0, rawMidi[0], rawMidi[1], rawMidi[2]};
        ui_->notifyMidiEvent(uiPacket);
    }

    if (bridgeSystem.isPaused()) {
        return Result::kFiltered;
    }

    // 3. Hardware Routing
    for (auto* t : transports_) {
        if (t != source && t->isConnected()) {
            t->sendMidi(rawMidi, 3);
        }
    }

    return Result::kForwarded;
}

void MidiBridge::onMidiReceived(Transport* source, const uint8_t* data, size_t length)
{
    // Counters
    if (source && strcmp(source->name(), "USB-HOST") == 0) {
        counters_.usbPacketsSeen++;
    }
    
    // Route it
    route(source, data, length);
}

MidiBridge::Result MidiBridge::forward(const uint8_t* data, size_t length, uint8_t outMidiPacket[4])
{
    // Legacy shim
    (void)outMidiPacket;
    // We assume 'data' here is 4-byte USB (CIN+Status+D1+D2)
    if (length >= 4) {
        onMidiReceived(nullptr, data + 1, 3);
    }
    return Result::kForwarded;
}
