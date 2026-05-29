#include "BridgeUi.h"

#include "BLEConnection.h"
#include "BridgeSettings.h"
#include "MidiCodec.h"
#include "RTPMidiConfig.h"
#include "NetworkServices.h"
#include "bongo_cat/BongoCat.h"

BridgeUi bridgeUi;

namespace {

#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 0
#endif

#ifndef BOARD_BTN_UP
#define BOARD_BTN_UP 10
#endif

#ifndef BOARD_BTN_DOWN
#define BOARD_BTN_DOWN 11
#endif

#ifndef BOARD_BTN_MENU
#define BOARD_BTN_MENU 14
#endif

constexpr uint16_t kShortPressMaxMs = 800;
constexpr uint16_t kOkPanicHoldMs = 1000;
constexpr uint16_t kOkPauseHoldMs = 2500;
constexpr uint16_t kMenuLongHoldMs = 1000;
constexpr uint16_t kMenuWifiSetupHoldMs = 4000;
constexpr uint32_t kToastDurationMs = 2000;

bool isButtonPressed(int8_t pin)
{
    return pin >= 0 && digitalRead(pin) == LOW;
}

}  // namespace

constexpr int16_t kKeyboardY = BongoCatDisplay::kOriginY + BongoCatDisplay::kDrawSize - 16;
constexpr int16_t kKeyboardX = 18;
constexpr int16_t kKeyboardW = 204;
constexpr int16_t kKeyboardH = 14;
constexpr uint8_t kKeyboardFirstNote = 48;  // C3
constexpr int16_t kKeyboardKeyCount = 24;   // C3..B4

#ifndef BACKLIGHT_DIM_LEVEL
#define BACKLIGHT_DIM_LEVEL 24
#endif

static const char* kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

void BridgeUi::begin(Arduino_GFX* display, int16_t blPin)
{
    gfx = display;
    backlightPin = blPin;
    lastActivityMs_ = millis();
    initBoardButtons();

    if (backlightPin >= 0) {
        ledcAttach(static_cast<uint8_t>(backlightPin), 5000, 8);
        setBacklight(255);
    }
}

void BridgeUi::setBle(BLEConnection* connection)
{
    ble = connection;
}

void BridgeUi::applySavedDisplayMode(uint8_t modeIndex)
{
    if (modeIndex >= static_cast<uint8_t>(DisplayMode::kModeCount)) {
        modeIndex = 0;
    }
    displayMode_ = static_cast<DisplayMode>(modeIndex);
}

uint32_t BridgeUi::backlightDimMs() const
{
    return bridgeSettings.backlightDimMs();
}

void BridgeUi::touchActivity(uint32_t nowMs)
{
    lastActivityMs_ = nowMs;
    if (backlightLevel_ < 200) {
        setBacklight(255);
    }
}

void BridgeUi::setBacklight(uint8_t level)
{
    backlightLevel_ = level;
    if (backlightPin >= 0) {
        ledcWrite(static_cast<uint8_t>(backlightPin), level);
    }
}

void BridgeUi::onNoteEvent(bool noteOn, uint8_t note, uint8_t velocity)
{
    lastVelocity_ = velocity;
    lastNote_ = note;
    refreshLastNoteLabel();

    if (note < 128) {
        if (noteOn && !heldNotes[note]) {
            heldNotes[note] = true;
            heldCount_++;
        } else if (!noteOn && heldNotes[note]) {
            heldNotes[note] = false;
            if (heldCount_ > 0) {
                heldCount_--;
            }
        }
    }

    if (noteOn) {
        noteOnTimes[noteOnHead_ % 32] = millis();
        noteOnHead_++;
    }
}

void BridgeUi::onControlChange(uint8_t cc, uint8_t value)
{
    if (cc == 64) {
        sustainDown_ = value >= 64;
    }
}

void BridgeUi::refreshLastNoteLabel()
{
    const int octave = static_cast<int>(lastNote_ / 12) - 1;
    snprintf(lastNoteLabel_, sizeof(lastNoteLabel_), "%s%d", kNoteNames[lastNote_ % 12], octave);
}

uint8_t BridgeUi::heldNoteCount() const
{
    return heldCount_;
}

uint16_t BridgeUi::notesPerMinute() const
{
    return notesPerMinute_;
}

void BridgeUi::updateNotesPerMinute(uint32_t nowMs)
{
    uint16_t count = 0;
    for (uint8_t i = 0; i < 32; i++) {
        if (noteOnTimes[i] != 0 && nowMs - noteOnTimes[i] < 60000) {
            count++;
        }
    }
    notesPerMinute_ = count;
}

const char* BridgeUi::displayModeName() const
{
    switch (displayMode_) {
        case DisplayMode::kPerformance:
            return "PERF";
        case DisplayMode::kMinimal:
            return "MIN";
        case DisplayMode::kStage:
            return "STAGE";
        default:
            return "FULL";
    }
}

void BridgeUi::cycleDisplayMode()
{
    const uint8_t next = (static_cast<uint8_t>(displayMode_) + 1) % static_cast<uint8_t>(DisplayMode::kModeCount);
    displayMode_ = static_cast<DisplayMode>(next);
    bridgeSettings.saveDisplayMode(static_cast<uint8_t>(displayMode_));
    Serial.printf("[UI] Display mode: %s\n", displayModeName());
}

void BridgeUi::toggleBridgePaused()
{
    bridgePaused_ = !bridgePaused_;
    Serial.printf("[UI] MIDI bridge %s\n", bridgePaused_ ? "PAUSED" : "running");
}

void BridgeUi::sendAllNotesOff()
{
    if (ble == nullptr || !ble->isConnected()) {
        Serial.println("[UI] Panic ignored (BLE not connected)");
        return;
    }

    for (uint8_t ch = 0; ch < 16; ch++) {
        uint8_t packet[5] = {0};
        size_t len = 0;
        MidiCodec::appendBleTimestamp(packet, &len, static_cast<uint16_t>(millis()));
        packet[len++] = static_cast<uint8_t>(0xB0 | ch);
        packet[len++] = 123;
        packet[len++] = 0;
        ble->sendMidi(packet, len);

        MidiCodec::appendBleTimestamp(packet, &len, static_cast<uint16_t>(millis()));
        packet[len++] = static_cast<uint8_t>(0xB0 | ch);
        packet[len++] = 120;
        packet[len++] = 0;
        ble->sendMidi(packet, len);
    }

    for (uint8_t n = 0; n < 128; n++) {
        heldNotes[n] = false;
    }
    heldCount_ = 0;
    sustainDown_ = false;
    Serial.println("[UI] Sent All Notes Off to BLE");
}

bool BridgeUi::shouldDrawFullMetrics() const
{
    return displayMode_ == DisplayMode::kFull;
}

bool BridgeUi::shouldDrawStatusPanel() const
{
    return displayMode_ != DisplayMode::kStage;
}

void BridgeUi::showToast(const char* text, uint32_t nowMs)
{
    if (text == nullptr || text[0] == '\0') {
        toastUntilMs_ = 0;
        toastText_[0] = '\0';
        return;
    }
    strncpy(toastText_, text, sizeof(toastText_) - 1);
    toastText_[sizeof(toastText_) - 1] = '\0';
    toastUntilMs_ = nowMs + kToastDurationMs;
}

void BridgeUi::initBoardButtons()
{
    okButton_.pin = BOOT_BUTTON_PIN;
    upButton_.pin = BOARD_BTN_UP;
    downButton_.pin = BOARD_BTN_DOWN;
    menuButton_.pin = BOARD_BTN_MENU;

    const int8_t pins[] = {okButton_.pin, upButton_.pin, downButton_.pin, menuButton_.pin};
    for (int8_t pin : pins) {
        if (pin >= 0) {
            pinMode(pin, INPUT_PULLUP);
        }
    }
}

void BridgeUi::handleBoardButtons(uint32_t nowMs)
{
    const bool okPressed = isButtonPressed(okButton_.pin);
    if (okPressed && !okButton_.down) {
        okButton_.down = true;
        okButton_.downMs = nowMs;
        okButton_.actionFired = false;
        pauseToggleFired_ = false;
    }

    if (!okPressed && okButton_.down) {
        if (!okButton_.actionFired && nowMs - okButton_.downMs < kShortPressMaxMs) {
            cycleDisplayMode();
            showToast(displayModeName(), nowMs);
        }
        okButton_.down = false;
        okButton_.actionFired = false;
        pauseToggleFired_ = false;
        return;
    }

    if (okPressed && okButton_.down && nowMs - okButton_.downMs >= kOkPanicHoldMs && !okButton_.actionFired) {
        okButton_.actionFired = true;
        sendAllNotesOff();
        showToast("PANIC", nowMs);
    }

    if (okPressed && okButton_.down && nowMs - okButton_.downMs >= kOkPauseHoldMs && !pauseToggleFired_) {
        pauseToggleFired_ = true;
        toggleBridgePaused();
        okButton_.actionFired = true;
        showToast(bridgePaused_ ? "PAUSED" : "RUNNING", nowMs);
    }

    const bool upPressed = isButtonPressed(upButton_.pin);
    if (upPressed && !upButton_.down) {
        upButton_.down = true;
        upButton_.downMs = nowMs;
        upButton_.actionFired = false;
    }
    if (!upPressed && upButton_.down) {
        if (!upButton_.actionFired && nowMs - upButton_.downMs < kShortPressMaxMs) {
            bridgeSettings.stepTranspose(1);
            char toast[24];
            snprintf(toast, sizeof(toast), "Transpose %+d", bridgeSettings.transposeSemitones());
            showToast(toast, nowMs);
        }
        upButton_.down = false;
        upButton_.actionFired = false;
    }

    const bool downPressed = isButtonPressed(downButton_.pin);
    if (downPressed && !downButton_.down) {
        downButton_.down = true;
        downButton_.downMs = nowMs;
        downButton_.actionFired = false;
    }
    if (!downPressed && downButton_.down) {
        if (!downButton_.actionFired && nowMs - downButton_.downMs < kShortPressMaxMs) {
            bridgeSettings.stepTranspose(-1);
            char toast[24];
            snprintf(toast, sizeof(toast), "Transpose %+d", bridgeSettings.transposeSemitones());
            showToast(toast, nowMs);
        }
        downButton_.down = false;
        downButton_.actionFired = false;
    }

    const bool menuPressed = isButtonPressed(menuButton_.pin);
    if (menuPressed && !menuButton_.down) {
        menuButton_.down = true;
        menuButton_.downMs = nowMs;
        menuButton_.actionFired = false;
        menuWifiSetupFired_ = false;
    }

    if (!menuPressed && menuButton_.down) {
        if (!menuButton_.actionFired && nowMs - menuButton_.downMs < kShortPressMaxMs) {
            bridgeSettings.cycleMidiChannelFilter();
            char toast[24];
            if (bridgeSettings.midiChannelFilter() == 0) {
                snprintf(toast, sizeof(toast), "MIDI: all ch");
            } else {
                snprintf(toast, sizeof(toast), "MIDI: ch%u only", bridgeSettings.midiChannelFilter());
            }
            showToast(toast, nowMs);
        }
        menuButton_.down = false;
        menuButton_.actionFired = false;
        menuWifiSetupFired_ = false;
        return;
    }

    if (menuPressed && menuButton_.down && nowMs - menuButton_.downMs >= kMenuWifiSetupHoldMs && !menuWifiSetupFired_) {
        menuWifiSetupFired_ = true;
        menuButton_.actionFired = true;
#if ENABLE_RTP_MIDI
        networkServices.enterSetupMode();
#endif
        showToast("WiFi SETUP", nowMs);
        return;
    }

    if (menuPressed && menuButton_.down && nowMs - menuButton_.downMs >= kMenuLongHoldMs && !menuButton_.actionFired) {
        menuButton_.actionFired = true;
        bridgeSettings.cycleBacklightDim();
        char toast[24];
        if (bridgeSettings.backlightDimMs() == 0) {
            snprintf(toast, sizeof(toast), "Dim: never");
        } else {
            snprintf(toast, sizeof(toast), "Dim: %lus", bridgeSettings.backlightDimMs() / 1000);
        }
        showToast(toast, nowMs);
    }
}

void BridgeUi::drawToast(uint32_t nowMs)
{
    if (gfx == nullptr || nowMs >= toastUntilMs_ || toastText_[0] == '\0') {
        return;
    }

    constexpr int16_t toastY = 218;
    gfx->fillRoundRect(24, toastY, 192, 18, 6, RGB565(24, 24, 48));
    gfx->drawRoundRect(24, toastY, 192, 18, 6, RGB565_CYAN);
    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_WHITE);
    gfx->setCursor(32, toastY + 5);
    gfx->print(toastText_);
}

void BridgeUi::tick(uint32_t nowMs)
{
    handleBoardButtons(nowMs);
    updateNotesPerMinute(nowMs);

    const uint32_t dimMs = backlightDimMs();
    if (backlightPin >= 0 && dimMs > 0) {
        if (nowMs - lastActivityMs_ > dimMs) {
            if (backlightLevel_ > BACKLIGHT_DIM_LEVEL) {
                setBacklight(BACKLIGHT_DIM_LEVEL);
            }
        }
    }
}

void BridgeUi::drawMiniKeyboard()
{
    if (gfx == nullptr || displayMode_ == DisplayMode::kMinimal || displayMode_ == DisplayMode::kStage) {
        return;
    }

    const int16_t keyW = kKeyboardW / kKeyboardKeyCount;
    gfx->fillRect(kKeyboardX, kKeyboardY, kKeyboardW, kKeyboardH, RGB565(12, 12, 20));

    for (uint8_t i = 0; i < kKeyboardKeyCount; i++) {
        const uint8_t note = kKeyboardFirstNote + i;
        const bool blackKey = ((i + 1) % 12 == 2 || (i + 1) % 12 == 4 || (i + 1) % 12 == 7 ||
                               (i + 1) % 12 == 9 || (i + 1) % 12 == 11);
        const bool held = note < 128 && heldNotes[note];
        const int16_t x = kKeyboardX + i * keyW;
        const uint16_t fill = held ? RGB565(80, 220, 120) : (blackKey ? RGB565(28, 28, 36) : RGB565(70, 70, 88));
        gfx->fillRect(x + 1, kKeyboardY + 1, keyW - 2, kKeyboardH - 2, fill);
        if (held) {
            gfx->drawRect(x, kKeyboardY, keyW, kKeyboardH, RGB565_LIME);
        }
    }
}

void BridgeUi::drawVelocityBar()
{
    if (gfx == nullptr || displayMode_ == DisplayMode::kMinimal || displayMode_ == DisplayMode::kStage) {
        return;
    }

    constexpr int16_t barX = 186;
    constexpr int16_t barY = 14;
    constexpr int16_t barW = 44;
    constexpr int16_t barH = 10;

    gfx->drawRect(barX, barY, barW, barH, RGB565(48, 48, 64));
    const int16_t fillW = (static_cast<int16_t>(lastVelocity_) * (barW - 2)) / 127;
    if (fillW > 0) {
        const uint16_t color = lastVelocity_ > 100 ? RGB565(255, 96, 64) : RGB565(64, 160, 255);
        gfx->fillRect(barX + 1, barY + 1, fillW, barH - 2, color);
    }
}

void BridgeUi::drawStatusChips(uint32_t nowMs)
{
    if (gfx == nullptr) {
        return;
    }

    (void)nowMs;

    constexpr int16_t chipY = 14;
    if (sustainDown_) {
        gfx->fillRoundRect(148, chipY, 34, 12, 4, RGB565(64, 32, 0));
        gfx->setTextSize(1);
        gfx->setTextColor(RGB565_GOLD);
        gfx->setCursor(152, chipY + 2);
        gfx->print("SUS");
    }

    if (bridgePaused_) {
        gfx->fillRoundRect(56, BongoCatDisplay::kOriginY + 48, 128, 22, 6, RGB565(96, 16, 16));
        gfx->setTextSize(1);
        gfx->setTextColor(RGB565_WHITE);
        gfx->setCursor(78, BongoCatDisplay::kOriginY + 55);
        gfx->print("MIDI PAUSED");
    }

    if (displayMode_ != DisplayMode::kFull && displayMode_ != DisplayMode::kStage) {
        gfx->setTextSize(1);
        gfx->setTextColor(RGB565_DARKGRAY);
        gfx->setCursor(186, 26);
        gfx->print(displayModeName());
    }

    if (displayMode_ == DisplayMode::kFull && ble != nullptr && ble->isConnected() && ble->getAverageLatencyMs() > 0) {
        char link[24] = {0};
        snprintf(link, sizeof(link), "fwd %ums", ble->getAverageLatencyMs());
        gfx->setTextSize(1);
        gfx->setTextColor(RGB565_DARKGRAY);
        gfx->setCursor(118, chipY + 2);
        gfx->print(link);
    }
}

void BridgeUi::drawOverlays(uint32_t nowMs)
{
    touchActivity(nowMs);
    drawVelocityBar();
    drawMiniKeyboard();
    drawStatusChips(nowMs);
    drawToast(nowMs);

    if (displayMode_ == DisplayMode::kPerformance && gfx != nullptr) {
        gfx->setTextSize(2);
        gfx->setTextColor(RGB565_WHITE);
        gfx->setCursor(22, BongoCatDisplay::kStatusTop - 18);
        gfx->print(lastNoteLabel_);
        char vel[8];
        snprintf(vel, sizeof(vel), "v%u", lastVelocity_);
        gfx->setTextSize(1);
        gfx->setTextColor(RGB565_LIGHTGRAY);
        gfx->setCursor(90, BongoCatDisplay::kStatusTop - 14);
        gfx->print(vel);
    }
}
