#include "BridgeUi.h"

#include "BLEConnection.h"
#include "Board.h"
#include "BridgeSettings.h"
#include "MidiBridge.h"
#include "MidiCodec.h"
#include "MidiEngine.h"
#include "RTPMidiConfig.h"
#include "USBConnection.h"
#include "ConnectivityManager.h"
#include "animation/BongoCat.h"

BridgeUi bridgeUi;
static constexpr int16_t kSidebarW = 54;

void BridgeUi::begin(Arduino_GFX* gfx_ptr, int16_t backlight_pin)
{
    gfx = gfx_ptr;
    lastActivityMs_ = millis();
}

void BridgeUi::setBle(BLEConnection* connection) { ble = connection; }
void BridgeUi::setUsbMidi(USBConnection* usb) { usb_ = usb; }
void BridgeUi::setMidiBridge(MidiBridge* bridge) { midiBridge_ = bridge; }
void BridgeUi::setMidiEngine(MidiEngine* engine) { engine_ = engine; }
void BridgeUi::setBongoCat(BongoCatDisplay* bongoCat) { bongoCat_ = bongoCat; }

void BridgeUi::setBoard(Board* board)
{
    board_ = board;
    if (board_) {
        okButton_.pin = board_->getButtonPin("OK");
        upButton_.pin = board_->getButtonPin("UP");
        downButton_.pin = board_->getButtonPin("DOWN");
        menuButton_.pin = board_->getButtonPin("MENU");
    }
}

void BridgeUi::notifyUsbStatus(bool connected) { usbConnected_ = connected; }
void BridgeUi::notifyBleStatus(bool connected) { bleConnected_ = connected; }

void BridgeUi::notifyRawMidiEvent(const uint8_t* data, size_t length)
{
    uint8_t usbPacket[4] = {0, 0, 0, 0};
    if (length >= 1) usbPacket[1] = data[0];
    if (length >= 2) usbPacket[2] = data[1];
    if (length >= 3) usbPacket[3] = data[2];
    notifyMidiEvent(usbPacket);
}

void BridgeUi::notifyMidiEvent(const uint8_t* data)
{
    const uint8_t status = data[1];
    const uint8_t messageType = status & 0xF0;
    const uint8_t channel = (status & 0x0F) + 1;
    const uint8_t data1 = data[2];
    const uint8_t data2 = data[3];
    char noteBuffer[12] = {0};
    char logLine[32] = {0};

    if (messageType == 0x90 || messageType == 0x80) {
        const bool noteOn = (messageType == 0x90 && data2 > 0);
        MidiCodec::noteName(data1, noteBuffer, sizeof(noteBuffer));
        snprintf(logLine, sizeof(logLine), "%-3s %s (v%u)", noteOn ? "ON" : "OFF", noteBuffer, data2);
        addLogEntry(logLine, noteOn ? RGB565_LIME : RGB565_DARKGRAY);
    } else if (messageType == 0xB0) {
        snprintf(logLine, sizeof(logLine), "CC %u=%u", data1, data2);
        addLogEntry(logLine, RGB565_GOLD);
    } else if (messageType == 0xE0) {
        addLogEntry("Pitch Bend", RGB565_CYAN);
    } else {
        addLogEntry(MidiCodec::statusName(status), RGB565_LIGHTGRAY);
    }
    lastMidiMs_ = millis();
    lastActivityMs_ = lastMidiMs_;
    setBacklight(255);
}

void BridgeUi::applySavedDisplayMode(uint8_t modeIndex)
{
    if (modeIndex >= static_cast<uint8_t>(DisplayMode::kModeCount)) modeIndex = 0;
    displayMode_ = static_cast<DisplayMode>(modeIndex);
}

uint32_t BridgeUi::backlightDimMs() const { return bridgeSettings.backlightDimMs(); }

void BridgeUi::addLogEntry(const char* text, uint16_t color)
{
    if (text == nullptr) return;
    MidiLogEntry& e = logEntries_[logHead_];
    strncpy(e.text, text, sizeof(e.text) - 1);
    e.text[sizeof(e.text) - 1] = '\0';
    e.color = color;
    e.timestamp = millis();
    logHead_ = (logHead_ + 1) % kMaxLogEntries;
    if (logCount_ < kMaxLogEntries) logCount_++;
}

void BridgeUi::tick(uint32_t nowMs)
{
    if (engine_) engine_->tick(nowMs);
    handleBoardButtons(nowMs);

    const uint32_t dimMs = backlightDimMs();
    if (board_ && dimMs > 0 && nowMs - lastActivityMs_ > dimMs) {
        setBacklight(40); // Dim level
    }
}

void BridgeUi::refresh(uint32_t nowMs, bool force)
{
    if (gfx == nullptr || engine_ == nullptr) return;
    if (!force && nowMs - lastRefreshMs_ < 50) return;
    lastRefreshMs_ = nowMs;

    const bool usbOk = usb_ && usb_->isConnected();
    const bool bleOk = ble && ble->isConnected();
    const auto& me = engine_->state();

    gfx->fillScreen(RGB565_BLACK);

    // Sidebar
    constexpr uint16_t kSideColor = RGB565(15, 15, 20);
    constexpr uint16_t kBorderColor = RGB565(40, 40, 50);
    gfx->fillRect(0, 0, kSidebarW, 240, kSideColor);
    gfx->drawFastVLine(kSidebarW, 0, 240, kBorderColor);

    auto drawMetric = [&](int16_t y, const char* label, const char* value, uint16_t color) {
        gfx->setTextColor(RGB565(80, 80, 100));
        gfx->setTextSize(1); gfx->setCursor(8, y); gfx->print(label);
        gfx->setTextColor(color);
        gfx->setTextSize(2); gfx->setCursor(10, y + 10); gfx->print(value);
    };

    drawMetric(12, "TRANS", (String(engine_->transpose() > 0 ? "+" : "") + String(engine_->transpose())).c_str(), 
               engine_->transpose() != 0 ? RGB565_GOLD : RGB565(120, 120, 140));
    
    char chanBuf[8];
    if (engine_->channelFilter() == 0) strcpy(chanBuf, "ALL"); else sprintf(chanBuf, "CH%u", engine_->channelFilter());
    drawMetric(60, "FILTER", chanBuf, engine_->channelFilter() > 0 ? RGB565_GOLD : RGB565(120, 120, 140));
    drawMetric(108, "MODE", displayModeName(), RGB565_CYAN);

    if (board_) {
        const float v = board_->getBatteryVoltage();
        const uint16_t bColor = v > 3.7f ? RGB565_LIME : (v > 3.4f ? RGB565_GOLD : RGB565_RED);
        gfx->drawRect(12, 210, 24, 12, RGB565(60, 60, 70));
        gfx->fillRect(14, 212, (int)(((v-3.3f)/0.9f) * 20), 8, board_->isUsbPowered() ? RGB565_CYAN : bColor);
    }

    // Main Console
    const int16_t mainX = kSidebarW + 12;
    gfx->setTextColor(RGB565_WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(mainX, 10);
    gfx->print("MIDI STATION");
    gfx->drawFastHLine(mainX, 28, 240 - mainX - 12, kBorderColor);

    if (shouldDrawStatusPanel()) {
        auto drawSync = [&](int16_t x, int16_t y, const char* label, bool ok) {
            uint16_t color = ok ? RGB565_LIME : RGB565(100, 100, 0);
            gfx->drawRect(x, y, 78, 30, RGB565(30, 30, 40));
            gfx->setTextSize(1); gfx->setTextColor(RGB565(120, 120, 130));
            gfx->setCursor(x + 5, y + 4); gfx->print(label);
            gfx->setTextColor(color); gfx->setCursor(x + 5, y + 16);
            gfx->print(ok ? "[ ONLINE ]" : "[ OFFLINE ]");
        };
        drawSync(mainX, 36, "USB HOST", usbOk);
        drawSync(mainX + 84, 36, "BLE MIDI", bleOk);

        const int16_t logY = 78;
        gfx->setTextSize(1); gfx->setTextColor(RGB565(100, 100, 120));
        gfx->setCursor(mainX, logY); gfx->print("ACTIVITY STREAM");
        gfx->fillRect(mainX, logY + 12, 162, 66, RGB565(10, 10, 15));
        gfx->drawRect(mainX, logY + 12, 162, 66, RGB565(25, 25, 35));
        
        const uint16_t pulseColor = (nowMs / 1000) % 2 == 0 ? RGB565(40, 40, 60) : RGB565(20, 20, 30);
        gfx->fillRect(mainX + 155, logY + 15, 4, 4, pulseColor);

        for (uint8_t i = 0; i < logCount(); i++) {
            const auto* e = &logEntries()[(logHead_ - logCount_ + i + kMaxLogEntries) % kMaxLogEntries];
            gfx->setTextColor(e->color); gfx->setCursor(mainX + 6, logY + 17 + (i * 12));
            gfx->printf("> %s", e->text);
        }
    }

    const bool active = lastMidiMs_ > 0 && nowMs - lastMidiMs_ < 400;
    if (bongoCat_) {
        bongoCat_->update(nowMs, active, me.noteEventsSeen);
        bongoCat_->draw(gfx, 95); 
    }

    drawToast(nowMs);
}

void BridgeUi::handleBoardButtons(uint32_t nowMs)
{
    auto handle = [&](Button& b, const char* name, std::function<void()> onShort, std::function<void()> onLong = nullptr, uint32_t longMs = 1000) {
        if (b.pin < 0) return;
        bool pressed = (digitalRead(b.pin) == LOW);
        if (pressed && !b.down) { b.down = true; b.downMs = nowMs; b.actionFired = false; }
        if (!pressed && b.down) {
            if (!b.actionFired && nowMs - b.downMs < kShortPressMaxMs) onShort();
            b.down = false; b.actionFired = false;
        }
        if (pressed && b.down && onLong && !b.actionFired && nowMs - b.downMs >= longMs) {
            onLong(); b.actionFired = true;
        }
    };

    handle(okButton_, "OK", 
        [this, nowMs](){ cycleDisplayMode(); showToast(displayModeName(), nowMs); },
        [this, nowMs](){ sendAllNotesOff(); showToast("PANIC", nowMs); }, kOkPanicHoldMs);

    handle(upButton_, "UP", [this, nowMs](){
        engine_->setTranspose(engine_->transpose() + 1);
        char t[16]; snprintf(t, sizeof(t), "Trans %+d", engine_->transpose()); showToast(t, nowMs);
    });

    handle(downButton_, "DWN", [this, nowMs](){
        engine_->setTranspose(engine_->transpose() - 1);
        char t[16]; snprintf(t, sizeof(t), "Trans %+d", engine_->transpose()); showToast(t, nowMs);
    });

    handle(menuButton_, "MENU", 
        [this, nowMs](){ 
            bridgeSettings.cycleMidiChannelFilter();
            engine_->setChannelFilter(bridgeSettings.midiChannelFilter());
            showToast("CHAN CYCLE", nowMs);
        },
        [this, nowMs](){ connectivityManager.startProvisioning(); showToast("WIFI SETUP", nowMs); }, kMenuWifiSetupHoldMs);
}

void BridgeUi::setBacklight(uint8_t level) { if (board_) board_->setBacklight(level); }
void BridgeUi::cycleDisplayMode() { applySavedDisplayMode((static_cast<uint8_t>(displayMode_) + 1) % static_cast<uint8_t>(DisplayMode::kModeCount)); }
void BridgeUi::showToast(const char* text, uint32_t nowMs) { strncpy(toastText_, text, sizeof(toastText_)-1); toastUntilMs_ = nowMs + 2000; }
void BridgeUi::drawToast(uint32_t nowMs) {
    if (nowMs < toastUntilMs_ && gfx) {
        gfx->fillRoundRect(70, 200, 100, 24, 4, RGB565(30, 30, 60));
        gfx->setTextColor(RGB565_WHITE); gfx->setTextSize(1);
        gfx->setCursor(80, 208); gfx->print(toastText_);
    }
}
void BridgeUi::sendAllNotesOff() { /* Panic logic */ }

bool BridgeUi::shouldDrawFullMetrics() const {
    return displayMode_ == DisplayMode::kFull;
}

bool BridgeUi::shouldDrawStatusPanel() const {
    return displayMode_ == DisplayMode::kFull || displayMode_ == DisplayMode::kPerformance;
}

const char* BridgeUi::displayModeName() const
{
    switch (displayMode_) {
        case DisplayMode::kFull: return "FULL";
        case DisplayMode::kPerformance: return "PERF";
        case DisplayMode::kMinimal: return "MINI";
        case DisplayMode::kStage: return "STAGE";
        default: return "UNKNOWN";
    }
}
