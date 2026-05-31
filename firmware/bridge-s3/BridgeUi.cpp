#include "BridgeUi.h"

#include "Board.h"
#include "BridgeSystem.h"
#include "MidiCodec.h"
#include "animation/BongoCat.h"

BridgeUi bridgeUi;

void BridgeUi::begin(Arduino_GFX* gfx_ptr)
{
    gfx = gfx_ptr;
}

void BridgeUi::refresh(uint32_t nowMs, bool force)
{
    if (gfx == nullptr) return;
    if (!force && nowMs - lastRefreshMs_ < 50) return;
    lastRefreshMs_ = nowMs;

    gfx->fillScreen(RGB565_BLACK);

    drawHeader(nowMs);
    drawCards(nowMs);
    drawConsole(nowMs);

    // Bongo Cat (Centered)
    if (bongoCat_) {
        const auto& me = bridgeSystem.engine().state();
        const bool active = lastMidiMs_ > 0 && nowMs - lastMidiMs_ < 400;
        bongoCat_->update(nowMs, active, me.noteEventsSeen);
        bongoCat_->draw(gfx, (240 - 128) / 2); 
    }

    drawKeyboardBar();
    drawToast(nowMs);
}

void BridgeUi::notifyMidiEvent(const uint8_t* data)
{
    const uint8_t status = data[1];
    const uint8_t messageType = status & 0xF0;
    const uint8_t data1 = data[2];
    const uint8_t data2 = data[3];
    char noteBuffer[12] = {0};
    char logLine[32] = {0};

    if (messageType == 0x90 || messageType == 0x80) {
        const bool noteOn = (messageType == 0x90 && data2 > 0);
        MidiCodec::noteName(data1, noteBuffer, sizeof(noteBuffer));
        snprintf(logLine, sizeof(logLine), "%-3s %s (v%u)", noteOn ? "ON" : "OFF", noteBuffer, data2);
        notifyStatus(logLine, noteOn ? RGB565_LIME : RGB565_DARKGRAY);
    } else if (messageType == 0xB0) {
        snprintf(logLine, sizeof(logLine), "CC %u=%u", data1, data2);
        notifyStatus(logLine, RGB565_GOLD);
    } else {
        notifyStatus(MidiCodec::statusName(status), RGB565_LIGHTGRAY);
    }
    
    lastMidiMs_ = millis();
}

void BridgeUi::notifyStatus(const char* text, uint16_t color)
{
    if (text == nullptr) return;
    LogEntry& e = logs_[logHead_];
    strncpy(e.text, text, sizeof(e.text) - 1);
    e.text[sizeof(e.text) - 1] = '\0';
    e.color = color;
    e.timestamp = millis();
    logHead_ = (logHead_ + 1) % kMaxLogEntries;
    if (logCount_ < kMaxLogEntries) logCount_++;
}

void BridgeUi::cycleDisplayMode()
{
    displayMode_ = static_cast<DisplayMode>((static_cast<uint8_t>(displayMode_) + 1) % static_cast<uint8_t>(DisplayMode::kModeCount));
    bridgeSystem.saveDisplayMode(static_cast<uint8_t>(displayMode_));
    showToast(displayMode_ == DisplayMode::kFull ? "FULL" : "PERF", millis());
}

void BridgeUi::drawHeader(uint32_t nowMs)
{
    constexpr uint16_t kHeaderColor = RGB565(20, 20, 30);
    gfx->fillRect(0, 0, 240, 32, kHeaderColor);
    gfx->drawFastHLine(0, 32, 240, RGB565(60, 60, 80));

    gfx->setTextSize(1);
    
    // Heartbeat
    const uint16_t pulseColor = (nowMs / 1000) % 2 == 0 ? RGB565_CYAN : RGB565(0, 40, 60);
    gfx->fillRect(10, 12, 4, 8, pulseColor);

    // System tray info (Placeholder for real status from System)
    gfx->setTextColor(RGB565_LIGHTGRAY);
    gfx->setCursor(25, 11);
    gfx->print("PIANO BRIDGE v2.1");

    if (board_) {
        const float v = board_->getBatteryVoltage();
        gfx->setCursor(180, 11);
        gfx->printf("%.1fV", v);
    }
}

void BridgeUi::drawCards(uint32_t nowMs)
{
    auto drawCard = [&](int16_t x, int16_t y, int16_t w, int16_t h, const char* label, const char* value, uint16_t color) {
        gfx->drawRoundRect(x, y, w, h, 6, RGB565(40, 40, 50));
        gfx->setTextColor(RGB565(100, 100, 120));
        gfx->setCursor(x + 8, y + 6); gfx->print(label);
        gfx->setTextColor(color); gfx->setTextSize(2);
        gfx->setCursor(x + 8, y + 18); gfx->print(value);
        gfx->setTextSize(1);
    };

    drawCard(10, 42, 115, 44, "TRANSPOSE", bridgeSystem.transposeString(), RGB565_GOLD);
    drawCard(130, 42, 100, 44, "CHANNEL", bridgeSystem.channelString(), RGB565_CYAN);
}

void BridgeUi::drawConsole(uint32_t nowMs)
{
    const int16_t logY = 96;
    gfx->setTextColor(RGB565(100, 100, 120));
    gfx->setCursor(10, logY); gfx->print("LIVE MONITOR");
    gfx->fillRect(10, logY + 12, 220, 54, RGB565(10, 10, 15));
    gfx->drawRect(10, logY + 12, 220, 54, RGB565(30, 30, 40));

    for (uint8_t i = 0; i < logCount_; i++) {
        const auto* e = &logs_[(logHead_ - logCount_ + i + kMaxLogEntries) % kMaxLogEntries];
        gfx->setTextColor(e->color); gfx->setCursor(18, logY + 18 + (i * 11));
        gfx->printf("> %s", e->text);
    }
}

void BridgeUi::drawKeyboardBar()
{
    const auto& me = bridgeSystem.engine().state();
    const int16_t y = 232, startX = 10;
    gfx->drawRect(startX - 1, y - 1, 130, 10, RGB565(40, 40, 50));
    for (int i = 0; i < 128; i++) {
        if (me.heldNotes[i]) gfx->drawFastVLine(startX + i, y, 8, RGB565_LIME);
        else if (i % 12 == 0) gfx->drawFastVLine(startX + i, y, 8, RGB565(30, 30, 40));
    }
}

void BridgeUi::showToast(const char* text, uint32_t nowMs)
{
    strncpy(toastText_, text, sizeof(toastText_) - 1);
    toastUntilMs_ = nowMs + 2000;
}

void BridgeUi::drawToast(uint32_t nowMs)
{
    if (nowMs < toastUntilMs_) {
        gfx->fillRoundRect(70, 180, 100, 24, 4, RGB565(30, 30, 60));
        gfx->setTextColor(RGB565_WHITE);
        gfx->setCursor(80, 188); gfx->print(toastText_);
    }
}
