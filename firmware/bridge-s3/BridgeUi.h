#ifndef BRIDGE_UI_H
#define BRIDGE_UI_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

class Board;
class BongoCatDisplay;
class BridgeSystem;

class BridgeUi {
public:
    enum class DisplayMode : uint8_t {
        kFull = 0,
        kPerformance,
        kMinimal,
        kStage,
        kModeCount
    };

    void begin(Arduino_GFX* gfx);
    void setBoard(Board* board) { board_ = board; }
    void setBongoCat(BongoCatDisplay* bongoCat) { bongoCat_ = bongoCat; }

    /** @brief Main refresh loop. */
    void refresh(uint32_t nowMs, bool force = false);

    /** @brief Called by system for high-priority display notifications. */
    void notifyMidiEvent(const uint8_t* data);
    void notifyStatus(const char* text, uint16_t color);

    DisplayMode displayMode() const { return displayMode_; }
    void cycleDisplayMode();

private:
    void drawHeader(uint32_t nowMs);
    void drawCards(uint32_t nowMs);
    void drawConsole(uint32_t nowMs);
    void drawKeyboardBar();
    void drawToast(uint32_t nowMs);
    void showToast(const char* text, uint32_t nowMs);

    Arduino_GFX* gfx = nullptr;
    Board* board_ = nullptr;
    BongoCatDisplay* bongoCat_ = nullptr;
    
    DisplayMode displayMode_ = DisplayMode::kFull;
    uint32_t lastRefreshMs_ = 0;
    uint32_t lastMidiMs_ = 0;

    struct LogEntry {
        char text[32];
        uint16_t color;
        uint32_t timestamp;
    };
    static constexpr uint8_t kMaxLogEntries = 4;
    LogEntry logs_[kMaxLogEntries];
    uint8_t logHead_ = 0;
    uint8_t logCount_ = 0;

    char toastText_[24];
    uint32_t toastUntilMs_ = 0;
};

extern BridgeUi bridgeUi;

#endif
