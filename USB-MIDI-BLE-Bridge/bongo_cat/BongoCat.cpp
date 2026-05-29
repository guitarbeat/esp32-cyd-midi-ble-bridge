#include "BongoCat.h"

#include <esp_random.h>
#include <string.h>

#include "BongoSprite.h"
#include "BongoSprites.h"

namespace {

constexpr uint32_t kMidiIdleMs = 2500;
constexpr uint32_t kNoteWindowMs = 1200;
constexpr uint32_t kFastNotesThreshold = 6;
constexpr uint32_t kHappyNotesThreshold = 10;

constexpr uint32_t kIdleStage1Ms = 12000;
constexpr uint32_t kIdleStage2Ms = 8000;
constexpr uint32_t kIdleStage3Ms = 8000;

}  // namespace

void BongoCatDisplay::begin()
{
    randomSeed(esp_random());

    for (uint8_t i = 0; i < kLayerCount; i++) {
        layers[i] = nullptr;
    }

    const uint32_t nowMs = millis();
    setState(kIdleStage1, nowMs);
    blinkTimerMs = nowMs + random(2500, 6000);
    earTwitchTimerMs = nowMs + random(8000, 20000);
    lastNoteCount = 0;
    notesInWindow = 0;
    windowStartMs = nowMs;
}

void BongoCatDisplay::setState(AnimState newState, uint32_t nowMs)
{
    state = newState;
    stateStartMs = nowMs;

    layers[kLayerBody] = &standardbody1;
    layers[kLayerTable] = &table1;

    switch (newState) {
        case kIdleStage1:
            layers[kLayerFace] = &stock_face;
            layers[kLayerPaws] = &twopawsup;
            layers[kLayerEffects] = nullptr;
            pawAnimationActive = false;
            break;
        case kIdleStage2:
            layers[kLayerFace] = &stock_face;
            layers[kLayerPaws] = nullptr;
            layers[kLayerEffects] = nullptr;
            pawAnimationActive = false;
            break;
        case kIdleStage3:
        case kIdleStage4:
            layers[kLayerFace] = &sleepy_face;
            layers[kLayerPaws] = nullptr;
            layers[kLayerEffects] = (newState == kIdleStage4) ? &sleepy1 : nullptr;
            pawAnimationActive = false;
            effectFrame = 0;
            effectTimerMs = nowMs;
            break;
        case kTypingSlow:
        case kTypingNormal:
        case kTypingFast:
            layers[kLayerFace] = (notesInWindow >= kHappyNotesThreshold) ? &happy_face : &stock_face;
            layers[kLayerPaws] = &leftpawdown;
            layers[kLayerEffects] = nullptr;
            pawAnimationActive = true;
            pawFrame = 0;
            pawTimerMs = nowMs;
            pawSpeedMs = (newState == kTypingSlow) ? 220 : (newState == kTypingNormal ? 150 : 95);
            break;
    }
}

BongoCatDisplay::AnimState BongoCatDisplay::pickTypingState(uint32_t nowMs) const
{
    (void)nowMs;
    if (notesInWindow >= kFastNotesThreshold) {
        return kTypingFast;
    }
    if (notesInWindow >= 3) {
        return kTypingNormal;
    }
    return kTypingSlow;
}

void BongoCatDisplay::updateIdleProgression(uint32_t nowMs)
{
    if (lastMidiMs > 0 && nowMs - lastMidiMs < kMidiIdleMs) {
        return;
    }

    if (state == kIdleStage1 && nowMs - stateStartMs > kIdleStage1Ms) {
        setState(kIdleStage2, nowMs);
    } else if (state == kIdleStage2 && nowMs - stateStartMs > kIdleStage2Ms) {
        setState(kIdleStage3, nowMs);
    } else if (state == kIdleStage3 && nowMs - stateStartMs > kIdleStage3Ms) {
        setState(kIdleStage4, nowMs);
    }
}

void BongoCatDisplay::updatePawAnimation(uint32_t nowMs)
{
    if (!pawAnimationActive) {
        return;
    }

    if (nowMs - pawTimerMs < pawSpeedMs) {
        return;
    }

    pawFrame = (pawFrame + 1) % 4;
    switch (pawFrame) {
        case 0:
            layers[kLayerPaws] = &leftpawdown;
            layers[kLayerEffects] = (state == kTypingFast) ? &left_click_effect : nullptr;
            break;
        case 1:
            layers[kLayerPaws] = &twopawsup;
            layers[kLayerEffects] = nullptr;
            break;
        case 2:
            layers[kLayerPaws] = &rightpawdown;
            layers[kLayerEffects] = (state == kTypingFast) ? &right_click_effect : nullptr;
            break;
        case 3:
            layers[kLayerPaws] = &twopawsup;
            layers[kLayerEffects] = nullptr;
            break;
    }
    pawTimerMs = nowMs;
}

void BongoCatDisplay::updateBlink(uint32_t nowMs)
{
    const bool canBlink = (state != kIdleStage3 && state != kIdleStage4);

    if (!blinking && canBlink && nowMs >= blinkTimerMs) {
        blinking = true;
        blinkStartMs = nowMs;
        layers[kLayerFace] = &blink_face;
        return;
    }

    if (!blinking || nowMs - blinkStartMs <= 180) {
        return;
    }

    blinking = false;
    if (state == kIdleStage3 || state == kIdleStage4) {
        layers[kLayerFace] = &sleepy_face;
    } else if (pawAnimationActive && notesInWindow >= kHappyNotesThreshold) {
        layers[kLayerFace] = &happy_face;
    } else {
        layers[kLayerFace] = &stock_face;
    }

    if (canBlink) {
        blinkTimerMs = nowMs + random(3000, 8000);
    }
}

void BongoCatDisplay::updateEarTwitch(uint32_t nowMs)
{
    if (!earTwitching && nowMs >= earTwitchTimerMs) {
        earTwitching = true;
        earTwitchStartMs = nowMs;
        layers[kLayerBody] = &bodyeartwitch;
        return;
    }

    if (!earTwitching || nowMs - earTwitchStartMs <= 450) {
        return;
    }

    earTwitching = false;
    layers[kLayerBody] = &standardbody1;
    earTwitchTimerMs = nowMs + random(10000, 25000);
}

void BongoCatDisplay::updateSleepyEffects(uint32_t nowMs)
{
    if (state != kIdleStage4) {
        return;
    }

    if (nowMs - effectTimerMs < 900) {
        return;
    }

    effectFrame = (effectFrame + 1) % 3;
    switch (effectFrame) {
        case 0: layers[kLayerEffects] = &sleepy1; break;
        case 1: layers[kLayerEffects] = &sleepy2; break;
        default: layers[kLayerEffects] = &sleepy3; break;
    }
    effectTimerMs = nowMs;
}

void BongoCatDisplay::update(uint32_t nowMs, bool midiActiveNow, uint32_t noteEventsSeen)
{
    if (noteEventsSeen != lastNoteCount) {
        const uint32_t delta = noteEventsSeen - lastNoteCount;
        lastNoteCount = noteEventsSeen;
        notesInWindow += delta;
        lastMidiMs = nowMs;
    }

    if (nowMs - windowStartMs > kNoteWindowMs) {
        windowStartMs = nowMs;
        notesInWindow = 0;
    }

    const bool playing = midiActiveNow && (nowMs - lastMidiMs < kMidiIdleMs);

    if (playing) {
        const AnimState desired = pickTypingState(nowMs);
        if (!pawAnimationActive || state != desired) {
            setState(desired, nowMs);
        }
    } else if (pawAnimationActive) {
        setState(kIdleStage1, nowMs);
    }

    updateIdleProgression(nowMs);
    updatePawAnimation(nowMs);
    updateSleepyEffects(nowMs);
    updateBlink(nowMs);
    updateEarTwitch(nowMs);
}

void BongoCatDisplay::draw(Arduino_GFX* gfx)
{
    if (gfx == nullptr) {
        return;
    }

    static uint16_t frameBuffer[kDrawSize * kDrawSize];
    memset(frameBuffer, 0, sizeof(frameBuffer));

    static const Layer drawOrder[] = {
        kLayerBody, kLayerFace, kLayerTable, kLayerPaws, kLayerEffects
    };

    for (Layer layer : drawOrder) {
        if (layers[layer] != nullptr) {
            blitBongoSpriteRgb565(frameBuffer, kDrawSize, kDrawSize, 0, 0, layers[layer], kScale);
        }
    }

    gfx->fillRect(kOriginX - 2, kOriginY - 2, kDrawSize + 4, kDrawSize + 4, RGB565_BLACK);
    gfx->draw16bitRGBBitmap(kOriginX, kOriginY, frameBuffer, kDrawSize, kDrawSize);
}
