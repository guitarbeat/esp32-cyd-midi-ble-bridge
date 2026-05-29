#ifndef BRIDGE_UI_H
#define BRIDGE_UI_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

class BLEConnection;

class BridgeUi {
public:
    enum class DisplayMode : uint8_t {
        kFull = 0,
        kPerformance,
        kMinimal,
        kStage,
        kModeCount
    };

    void begin(Arduino_GFX* gfx, int16_t backlightPin);
    void applySavedDisplayMode(uint8_t modeIndex);
    void setBle(BLEConnection* ble);
    uint32_t backlightDimMs() const;

    void onNoteEvent(bool noteOn, uint8_t note, uint8_t velocity);
    void onControlChange(uint8_t cc, uint8_t value);
    void touchActivity(uint32_t nowMs);

    void tick(uint32_t nowMs);
    void drawOverlays(uint32_t nowMs);
    bool shouldDrawFullMetrics() const;
    bool shouldDrawStatusPanel() const;
    bool isBridgePaused() const { return bridgePaused_; }
    DisplayMode displayMode() const { return displayMode_; }

    uint8_t heldNoteCount() const;
    uint16_t notesPerMinute() const;
    uint8_t lastVelocity() const { return lastVelocity_; }
    bool sustainDown() const { return sustainDown_; }

    const char* displayModeName() const;
    const char* lastNoteLabel() const { return lastNoteLabel_; }

private:
    Arduino_GFX* gfx = nullptr;
    BLEConnection* ble = nullptr;
    int16_t backlightPin = -1;

    bool heldNotes[128] = {false};
    uint8_t heldCount_ = 0;
    uint8_t lastVelocity_ = 0;
    uint8_t lastNote_ = 0;
    bool sustainDown_ = false;
    char lastNoteLabel_[12] = "--";

    uint32_t noteOnTimes[32] = {0};
    uint8_t noteOnHead_ = 0;
    uint16_t notesPerMinute_ = 0;

    DisplayMode displayMode_ = DisplayMode::kFull;
    bool bridgePaused_ = false;
    uint8_t backlightLevel_ = 255;
    uint32_t lastActivityMs_ = 0;

    bool pauseToggleFired_ = false;
    bool menuWifiSetupFired_ = false;

    char toastText_[40] = {0};
    uint32_t toastUntilMs_ = 0;

    struct BoardButton {
        int8_t pin = -1;
        bool down = false;
        uint32_t downMs = 0;
        bool actionFired = false;
    };

    BoardButton okButton_;
    BoardButton upButton_;
    BoardButton downButton_;
    BoardButton menuButton_;

    void cycleDisplayMode();
    void toggleBridgePaused();
    void sendAllNotesOff();
    void showToast(const char* text, uint32_t nowMs);
    void updateNotesPerMinute(uint32_t nowMs);
    void refreshLastNoteLabel();
    void setBacklight(uint8_t level);
    void handleBoardButtons(uint32_t nowMs);
    void initBoardButtons();
    void drawToast(uint32_t nowMs);
    void drawMiniKeyboard();
    void drawVelocityBar();
    void drawStatusChips(uint32_t nowMs);
};

extern BridgeUi bridgeUi;

#endif
