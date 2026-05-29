#include "BridgeSettings.h"

#include <Preferences.h>

BridgeSettings bridgeSettings;

namespace {

constexpr char kNamespace[] = "piano_ble";
constexpr char kKeyBleName[] = "ble_name";
constexpr char kKeyDimMs[] = "dim_ms";
constexpr char kKeyTranspose[] = "transpose";
constexpr char kKeyChannel[] = "channel";
constexpr char kKeyDisplay[] = "display";
constexpr uint32_t kSchemaVersion = 1;
constexpr char kKeySchema[] = "schema";

}  // namespace

bool BridgeSettings::begin(const char* defaultBleName)
{
    if (defaultBleName != nullptr) {
        strncpy(bleName_, defaultBleName, kBleNameMax);
        bleName_[kBleNameMax] = '\0';
    }
    load();
    return true;
}

void BridgeSettings::load()
{
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) {
        return;
    }

    const uint32_t schema = prefs.getUInt(kKeySchema, 0);
    if (schema != kSchemaVersion) {
        prefs.end();
        saveAll();
        return;
    }

    const String name = prefs.getString(kKeyBleName, bleName_);
    strncpy(bleName_, name.c_str(), kBleNameMax);
    bleName_[kBleNameMax] = '\0';

    backlightDimMs_ = prefs.getUInt(kKeyDimMs, backlightDimMs_);
    transpose_ = static_cast<int8_t>(prefs.getChar(kKeyTranspose, transpose_));
    midiChannel_ = prefs.getUChar(kKeyChannel, midiChannel_);
    displayMode_ = prefs.getUChar(kKeyDisplay, displayMode_);

    if (transpose_ < kTransposeMin || transpose_ > kTransposeMax) {
        transpose_ = 0;
    }
    if (midiChannel_ > 16) {
        midiChannel_ = 0;
    }
    if (displayMode_ >= 4) {
        displayMode_ = 0;
    }

    prefs.end();
}

void BridgeSettings::saveAll()
{
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return;
    }

    prefs.putUInt(kKeySchema, kSchemaVersion);
    prefs.putString(kKeyBleName, bleName_);
    prefs.putUInt(kKeyDimMs, backlightDimMs_);
    prefs.putChar(kKeyTranspose, transpose_);
    prefs.putUChar(kKeyChannel, midiChannel_);
    prefs.putUChar(kKeyDisplay, displayMode_);
    prefs.end();
}

void BridgeSettings::saveDisplayMode(uint8_t mode)
{
    displayMode_ = mode;
    saveAll();
}

void BridgeSettings::stepTranspose(int8_t delta)
{
    const int next = static_cast<int>(transpose_) + static_cast<int>(delta);
    if (next < kTransposeMin || next > kTransposeMax) {
        return;
    }
    transpose_ = static_cast<int8_t>(next);
    saveAll();
    Serial.printf("[SETTINGS] Transpose %+d semitones\n", transpose_);
}

void BridgeSettings::cycleTranspose()
{
    transpose_++;
    if (transpose_ > kTransposeMax) {
        transpose_ = kTransposeMin;
    }
    saveAll();
    Serial.printf("[SETTINGS] Transpose %+d semitones\n", transpose_);
}

void BridgeSettings::cycleMidiChannelFilter()
{
    midiChannel_++;
    if (midiChannel_ > 16) {
        midiChannel_ = 0;
    }
    saveAll();
    if (midiChannel_ == 0) {
        Serial.println("[SETTINGS] MIDI channel filter: all");
    } else {
        Serial.printf("[SETTINGS] MIDI channel filter: ch%u only\n", midiChannel_);
    }
}

void BridgeSettings::cycleBacklightDim()
{
    static const uint32_t kPresets[] = {30000, 90000, 180000, 0};
    constexpr size_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);
    size_t next = 0;
    for (size_t i = 0; i < kPresetCount; i++) {
        if (kPresets[i] == backlightDimMs_) {
            next = (i + 1) % kPresetCount;
            break;
        }
    }
    backlightDimMs_ = kPresets[next];
    saveAll();
    if (backlightDimMs_ == 0) {
        Serial.println("[SETTINGS] Backlight dim: never");
    } else {
        Serial.printf("[SETTINGS] Backlight dim: %lus idle\n", backlightDimMs_ / 1000);
    }
}

uint8_t BridgeSettings::clampNote(int note)
{
    if (note < 0) {
        return 0;
    }
    if (note > 127) {
        return 127;
    }
    return static_cast<uint8_t>(note);
}

bool BridgeSettings::transformUsbMidiPacket(uint8_t* packet)
{
    const uint8_t status = packet[1];
    if (status == 0x00) {
        return false;
    }

    if (midiChannel_ > 0) {
        const uint8_t packetChannel = (status & 0x0F) + 1;
        if (packetChannel != midiChannel_) {
            return false;
        }
    }

    const uint8_t messageType = status & 0xF0;
    if (messageType == 0x90 || messageType == 0x80) {
        const int transposed = static_cast<int>(packet[2]) + transpose_;
        packet[2] = clampNote(transposed);
    }

    return true;
}

void BridgeSettings::printSummary() const
{
    Serial.println("[SETTINGS] ---");
    Serial.printf("  BLE name: %s (reboot to apply if changed)\n", bleName_);
    Serial.printf("  Transpose: %+d\n", transpose_);
    if (midiChannel_ == 0) {
        Serial.println("  MIDI channel: all");
    } else {
        Serial.printf("  MIDI channel: ch%u only\n", midiChannel_);
    }
    if (backlightDimMs_ == 0) {
        Serial.println("  Backlight dim: never");
    } else {
        Serial.printf("  Backlight dim: %lus idle\n", backlightDimMs_ / 1000);
    }
    Serial.printf("  Display mode index: %u\n", displayMode_);
    Serial.println("[SETTINGS] Buttons: UP/DOWN=transpose, MENU=ch, MENU hold=dim/wifi, OK=mode, OK hold=panic/pause");
}
