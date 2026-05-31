#ifndef BRIDGE_SYSTEM_H
#define BRIDGE_SYSTEM_H

#include <Arduino.h>
#include "BridgeSettings.h"
#include "MidiEngine.h"

/**
 * @brief The System Controller (Central Brain).
 * 
 * Deep Module Principle: Consolidates live state (Engine) and persistent state (Settings).
 * High Leverage: One call to setTranspose() updates the audio path AND saves to flash.
 * High Locality: Centralizes the source of truth for the entire device.
 */
class BridgeSystem {
public:
    void begin();
    
    // --- Live Components ---
    MidiEngine& engine() { return engine_; }
    BridgeSettings& settings() { return settings_; }

    // --- Command Surface (Deep Actions) ---
    void stepTranspose(int8_t delta);
    void cycleMidiChannel();
    void saveDisplayMode(uint8_t mode);
    void cycleBacklightDim();
    void togglePause();
    void sendPanic();

    // --- Query Surface for UI ---
    bool isPaused() const { return bridgePaused_; }
    const char* transposeString() const;
    const char* channelString() const;

    /** @brief Must be called in the main loop. */
    void tick(uint32_t nowMs);

private:
    MidiEngine engine_;
    BridgeSettings settings_;
    bool bridgePaused_ = false;

    char transposeBuf_[8];
    char channelBuf_[8];
};

extern BridgeSystem bridgeSystem;

#endif
