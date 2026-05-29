#ifndef BRIDGE_SETTINGS_H
#define BRIDGE_SETTINGS_H

#include <Arduino.h>

class BridgeSettings {
public:
    static constexpr uint8_t kBleNameMax = 31;
    static constexpr int8_t kTransposeMin = -12;
    static constexpr int8_t kTransposeMax = 12;

    bool begin(const char* defaultBleName);

    const char* bleDeviceName() const { return bleName_; }
    uint32_t backlightDimMs() const { return backlightDimMs_; }
    int8_t transposeSemitones() const { return transpose_; }
    uint8_t midiChannelFilter() const { return midiChannel_; }  // 0 = all channels
    uint8_t displayModeIndex() const { return displayMode_; }

    void saveDisplayMode(uint8_t mode);
    void stepTranspose(int8_t delta);
    void cycleTranspose();
    void cycleMidiChannelFilter();
    void cycleBacklightDim();

    bool transformUsbMidiPacket(uint8_t* packet);

    void printSummary() const;

private:
    char bleName_[kBleNameMax + 1] = {0};
    uint32_t backlightDimMs_ = 90000;
    int8_t transpose_ = 0;
    uint8_t midiChannel_ = 0;
    uint8_t displayMode_ = 0;

    void load();
    void saveAll();
    static uint8_t clampNote(int note);
};

extern BridgeSettings bridgeSettings;

#endif
