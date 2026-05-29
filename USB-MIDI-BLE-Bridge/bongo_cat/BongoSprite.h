#ifndef BONGO_SPRITE_H
#define BONGO_SPRITE_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// LVGL RGB565A8 sprite (from vostoklabs/bongo_cat_monitor, MIT).
// Memory layout: all RGB565 rows (w*2 bytes each), then all A8 rows (w bytes each).
struct BongoSprite {
    uint16_t width;
    uint16_t height;
    uint32_t data_size;
    const uint8_t* data;
};

void blitBongoSpriteRgb565(uint16_t* dest, uint16_t destW, uint16_t destH, int16_t destX, int16_t destY,
                           const BongoSprite* sprite, uint8_t scale, uint8_t alphaThreshold = 48);

void drawBongoSprite(Arduino_GFX* gfx, int16_t x, int16_t y, const BongoSprite* sprite, uint8_t scale = 1,
                     uint8_t alphaThreshold = 48);

#endif
