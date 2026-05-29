#include "BongoSprite.h"

namespace {

uint16_t readRgb565(const uint8_t* data, uint32_t colorIndex)
{
    const uint8_t lo = pgm_read_byte(&data[colorIndex]);
    const uint8_t hi = pgm_read_byte(&data[colorIndex + 1]);
    return static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8);
}

uint8_t readAlpha(const uint8_t* data, uint16_t w, uint16_t h, uint16_t row, uint16_t col)
{
    const uint32_t rgbPlaneSize = static_cast<uint32_t>(w) * h * 2;
    const uint32_t alphaIndex = rgbPlaneSize + static_cast<uint32_t>(row) * w + col;
    return pgm_read_byte(&data[alphaIndex]);
}

}  // namespace

void blitBongoSpriteRgb565(uint16_t* dest, uint16_t destW, uint16_t destH, int16_t destX, int16_t destY,
                           const BongoSprite* sprite, uint8_t scale, uint8_t alphaThreshold)
{
    if (dest == nullptr || sprite == nullptr || sprite->data == nullptr || scale == 0) {
        return;
    }

    const uint16_t w = sprite->width;
    const uint16_t h = sprite->height;
    const uint8_t* data = sprite->data;

    for (uint16_t row = 0; row < h; row++) {
        const uint32_t rgbRowBase = static_cast<uint32_t>(row) * w * 2;
        const int16_t dstRow = destY + static_cast<int16_t>(row) * scale;
        if (dstRow < 0 || dstRow >= static_cast<int16_t>(destH)) {
            continue;
        }

        for (uint16_t col = 0; col < w; col++) {
            if (readAlpha(data, w, h, row, col) <= alphaThreshold) {
                continue;
            }

            const int16_t dstCol = destX + static_cast<int16_t>(col) * scale;
            if (dstCol < 0 || dstCol >= static_cast<int16_t>(destW)) {
                continue;
            }

            const uint16_t color = readRgb565(data, rgbRowBase + static_cast<uint32_t>(col) * 2);
            for (uint8_t dy = 0; dy < scale; dy++) {
                const int16_t py = dstRow + dy;
                if (py < 0 || py >= static_cast<int16_t>(destH)) {
                    continue;
                }
                for (uint8_t dx = 0; dx < scale; dx++) {
                    const int16_t px = dstCol + dx;
                    if (px < 0 || px >= static_cast<int16_t>(destW)) {
                        continue;
                    }
                    dest[static_cast<uint32_t>(py) * destW + static_cast<uint32_t>(px)] = color;
                }
            }
        }
    }
}

void drawBongoSprite(Arduino_GFX* gfx, int16_t x, int16_t y, const BongoSprite* sprite, uint8_t scale,
                     uint8_t alphaThreshold)
{
    if (gfx == nullptr || sprite == nullptr || sprite->data == nullptr || scale == 0) {
        return;
    }

    const uint16_t w = sprite->width;
    const uint16_t h = sprite->height;
    const uint8_t* data = sprite->data;

    gfx->startWrite();
    for (uint16_t row = 0; row < h; row++) {
        const uint32_t rgbRowBase = static_cast<uint32_t>(row) * w * 2;
        for (uint16_t col = 0; col < w; col++) {
            if (readAlpha(data, w, h, row, col) <= alphaThreshold) {
                continue;
            }

            const uint16_t color = readRgb565(data, rgbRowBase + static_cast<uint32_t>(col) * 2);
            gfx->fillRect(x + static_cast<int16_t>(col) * scale, y + static_cast<int16_t>(row) * scale, scale,
                          scale, color);
        }
    }
    gfx->endWrite();
}
