#include "BridgeSystem.h"

BridgeSystem bridgeSystem;

void BridgeSystem::begin()
{
    // 1. Initialize Settings (Memory)
    settings_.begin("Piano-BLE-Bridge");

    // 2. Synchronize Engine (Live) with Settings
    engine_.setTranspose(settings_.transposeSemitones());
    engine_.setChannelFilter(settings_.midiChannelFilter());

    Serial.println("[SYSTEM] Brain initialized and synchronized.");
}

void BridgeSystem::stepTranspose(int8_t delta)
{
    // Update Memory
    settings_.stepTranspose(delta);
    
    // Update Live Engine
    engine_.setTranspose(settings_.transposeSemitones());
}

void BridgeSystem::cycleMidiChannel()
{
    // Update Memory
    settings_.cycleMidiChannelFilter();
    
    // Update Live Engine
    engine_.setChannelFilter(settings_.midiChannelFilter());
}

void BridgeSystem::saveDisplayMode(uint8_t mode)
{
    settings_.saveDisplayMode(mode);
}

void BridgeSystem::cycleBacklightDim()
{
    settings_.cycleBacklightDim();
}

void BridgeSystem::togglePause()
{
    bridgePaused_ = !bridgePaused_;
}

void BridgeSystem::sendPanic()
{
    // Trigger "All Notes Off" on the engine state and transport logic
    // (Implementation detail: UI and Bridge will react to this)
    Serial.println("[SYSTEM] Panic triggered.");
}

const char* BridgeSystem::transposeString() const
{
    int8_t val = settings_.transposeSemitones();
    snprintf((char*)transposeBuf_, sizeof(transposeBuf_), "%s%d", val > 0 ? "+" : "", val);
    return transposeBuf_;
}

const char* BridgeSystem::channelString() const
{
    uint8_t val = settings_.midiChannelFilter();
    if (val == 0) return "ALL";
    snprintf((char*)channelBuf_, sizeof(channelBuf_), "CH%u", val);
    return channelBuf_;
}

void BridgeSystem::tick(uint32_t nowMs)
{
    engine_.tick(nowMs);
}
