#ifndef BONGO_CAT_H
#define BONGO_CAT_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

struct BongoSprite;

// Sprite animation from https://github.com/vostoklabs/bongo_cat_monitor (MIT).
class BongoCatDisplay {
public:
    static constexpr uint8_t kScale = 2;  // 64px art -> 128px on 240x240 ST7789
    static constexpr int16_t kSpriteSize = 64;
    static constexpr int16_t kDrawSize = kSpriteSize * kScale;
    static constexpr int16_t kOriginX = (240 - kDrawSize) / 2;
    static constexpr int16_t kOriginY = 36;
    static constexpr int16_t kStatusTop = kOriginY + kDrawSize + 6;

    void begin();
    void update(uint32_t nowMs, bool midiActive, uint32_t noteEventsSeen);
    void draw(Arduino_GFX* gfx);

private:
    enum Layer : uint8_t {
        kLayerBody = 0,
        kLayerFace,
        kLayerTable,
        kLayerPaws,
        kLayerEffects,
        kLayerCount
    };

    enum AnimState : uint8_t {
        kIdleStage1 = 0,
        kIdleStage2,
        kIdleStage3,
        kIdleStage4,
        kTypingSlow,
        kTypingNormal,
        kTypingFast
    };

    const struct BongoSprite* layers[kLayerCount];
    AnimState state;
    uint32_t stateStartMs;
    uint32_t lastMidiMs;
    uint32_t lastNoteCount;
    uint32_t notesInWindow;
    uint32_t windowStartMs;

    uint32_t blinkTimerMs;
    uint32_t blinkStartMs;
    bool blinking;

    uint32_t earTwitchTimerMs;
    uint32_t earTwitchStartMs;
    bool earTwitching;

    uint8_t pawFrame;
    uint32_t pawTimerMs;
    bool pawAnimationActive;
    uint16_t pawSpeedMs;

    uint8_t effectFrame;
    uint32_t effectTimerMs;

    void setState(AnimState newState, uint32_t nowMs);
    void updateIdleProgression(uint32_t nowMs);
    void updatePawAnimation(uint32_t nowMs);
    void updateBlink(uint32_t nowMs);
    void updateEarTwitch(uint32_t nowMs);
    void updateSleepyEffects(uint32_t nowMs);
    AnimState pickTypingState(uint32_t nowMs) const;
};

#endif
