// Generic USB MIDI to BLE MIDI bridge for ESP32-S3 boards with USB-OTG.
//
// Direction: USB MIDI keyboard/controller -> ESP32-S3 -> BLE MIDI peripheral.
// iPhone/iPad apps such as GarageBand connect from their Bluetooth MIDI menu.

#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "USB-MIDI-BLE-Bridge requires an ESP32-S3 board with native USB-OTG host support. Classic ESP32 boards cannot host USB MIDI in firmware alone."
#endif

#include <Arduino_GFX_Library.h>
#include "USBConnection.h"
#include "BLEConnection.h"

#ifndef BLE_DEVICE_NAME_TEXT
#define BLE_DEVICE_NAME_TEXT "Piano BLE Bridge"
#endif

static const char* BLE_DEVICE_NAME = BLE_DEVICE_NAME_TEXT;
static const uint32_t SERIAL_BAUD = 115200;

// Official Espressif ESP32-S3-USB-OTG board controls. These are harmless on
// generic S3 boards only if the pins are unconnected; override to -1 if needed.
#ifndef USB_HOST_SEL_PIN
#define USB_HOST_SEL_PIN 18
#endif

#ifndef USB_HOST_VBUS_EN_PIN
#define USB_HOST_VBUS_EN_PIN 12
#endif

#ifndef USB_HOST_LIMIT_EN_PIN
#define USB_HOST_LIMIT_EN_PIN 17
#endif

#ifndef USB_HOST_BOOST_EN_PIN
#define USB_HOST_BOOST_EN_PIN 13
#endif

#ifndef USB_HOST_POWER_FROM_BATTERY
#define USB_HOST_POWER_FROM_BATTERY 0
#endif

#ifndef LCD_ENABLE_PIN
#define LCD_ENABLE_PIN 5
#endif

#ifndef LCD_RESET_PIN
#define LCD_RESET_PIN 8
#endif

#ifndef LCD_DC_PIN
#define LCD_DC_PIN 4
#endif

#ifndef LCD_SCLK_PIN
#define LCD_SCLK_PIN 6
#endif

#ifndef LCD_MOSI_PIN
#define LCD_MOSI_PIN 7
#endif

#ifndef LCD_BACKLIGHT_PIN
#define LCD_BACKLIGHT_PIN 9
#endif

static Arduino_DataBus* displayBus = new Arduino_ESP32SPI(
    LCD_DC_PIN,
    GFX_NOT_DEFINED,
    LCD_SCLK_PIN,
    LCD_MOSI_PIN,
    GFX_NOT_DEFINED);

static Arduino_GFX* display = new Arduino_ST7789(
    displayBus,
    LCD_RESET_PIN,
    0,
    true,
    240,
    240,
    0,
    0,
    0,
    0);

BLEConnection bleMidi;

static uint32_t usbPacketsSeen = 0;
static uint32_t midiEventsSeen = 0;
static uint32_t noteEventsSeen = 0;
static uint32_t blePacketsSent = 0;
static uint32_t blePacketsSkipped = 0;
static bool displayReady = false;
static uint32_t lastMidiMs = 0;
static char lastMidiText[36] = "none";
static bool activeNotes[128] = {false};
static uint8_t activeNotesCount = 0;
static bool displayRefreshPending = true;
static bool displayStaticDrawn = false;

static uint8_t midiLengthFromStatus(uint8_t status)
{
    if (status >= 0xF8) return 1; // Realtime messages.

    switch (status & 0xF0) {
        case 0xC0: // Program Change
        case 0xD0: // Channel Pressure
            return 2;
        case 0x80: // Note Off
        case 0x90: // Note On
        case 0xA0: // Poly Pressure
        case 0xB0: // Control Change
        case 0xE0: // Pitch Bend
            return 3;
        default:
            break;
    }

    switch (status) {
        case 0xF1: // MTC Quarter Frame
        case 0xF3: // Song Select
            return 2;
        case 0xF2: // Song Position Pointer
            return 3;
        case 0xF6: // Tune Request
            return 1;
        default:
            return 0;
    }
}

static uint8_t midiLengthFromUsbCin(uint8_t cin, uint8_t status)
{
    switch (cin & 0x0F) {
        case 0x2: return 2; // Two-byte system common.
        case 0x3: return 3; // Three-byte system common.
        case 0x4: return 3; // SysEx start/continue.
        case 0x5: return 1; // SysEx ends with one byte.
        case 0x6: return 2; // SysEx ends with two bytes.
        case 0x7: return 3; // SysEx ends with three bytes.
        case 0x8: return 3; // Note Off.
        case 0x9: return 3; // Note On.
        case 0xA: return 3; // Poly Pressure.
        case 0xB: return 3; // Control Change.
        case 0xC: return 2; // Program Change.
        case 0xD: return 2; // Channel Pressure.
        case 0xE: return 3; // Pitch Bend.
        case 0xF: return 1; // Single byte.
        default:
            return midiLengthFromStatus(status);
    }
}

static bool buildBleMidiPacket(const uint8_t* usbMidiPacket, size_t usbLength, uint8_t* blePacket, size_t* bleLength)
{
    if (usbLength < 4 || blePacket == nullptr || bleLength == nullptr) {
        return false;
    }

    const uint8_t cin = usbMidiPacket[0] & 0x0F;
    const uint8_t status = usbMidiPacket[1];
    const uint8_t midiLength = midiLengthFromUsbCin(cin, status);

    if (midiLength == 0 || midiLength > 3 || status == 0x00) {
        return false;
    }

    const uint16_t timestamp = millis() & 0x1FFF;
    blePacket[0] = 0x80 | ((timestamp >> 7) & 0x3F);
    blePacket[1] = 0x80 | (timestamp & 0x7F);

    for (uint8_t i = 0; i < midiLength; i++) {
        blePacket[2 + i] = usbMidiPacket[1 + i];
    }

    *bleLength = 2 + midiLength;
    return true;
}

static const char* statusName(uint8_t status)
{
    switch (status & 0xF0) {
        case 0x80: return "NoteOff";
        case 0x90: return "NoteOn";
        case 0xA0: return "PolyPressure";
        case 0xB0: return "ControlChange";
        case 0xC0: return "ProgramChange";
        case 0xD0: return "ChannelPressure";
        case 0xE0: return "PitchBend";
        default: return "System";
    }
}

static const char* noteName(uint8_t note, char* buffer, size_t bufferLength)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int octave = (note / 12) - 1;
    snprintf(buffer, bufferLength, "%s%d", names[note % 12], octave);
    return buffer;
}

static void updateDisplayDashboard(bool force = false);
static void markDisplayMidiEvent(const uint8_t* data);

class UsbMidiInput : public USBConnection {
public:
    void onDeviceConnected() override
    {
        Serial.println("[USB] MIDI device connected.");
        if (getLastError().length() > 0) {
            Serial.println("[USB] Last error: " + getLastError());
        }
    }

    void onDeviceDisconnected() override
    {
        Serial.println("[USB] MIDI device disconnected.");
    }

    void onMidiDataReceived(const uint8_t* data, size_t length) override
    {
        usbPacketsSeen++;

        uint8_t blePacket[5] = {0};
        size_t bleLength = 0;

        if (!buildBleMidiPacket(data, length, blePacket, &bleLength)) {
            if (length >= 2) {
                Serial.printf("[USB] Ignored packet CIN=%02X status=%02X\n", data[0] & 0x0F, data[1]);
            } else {
                Serial.printf("[USB] Ignored short packet length=%u\n", static_cast<unsigned>(length));
            }
            return;
        }

        Serial.printf("[USB] %-15s ch=%u data1=%3u data2=%3u BLE=%s\n",
                      statusName(data[1]),
                      ((data[1] & 0x0F) + 1),
                      data[2],
                      data[3],
                      bleMidi.isConnected() ? "connected" : "waiting");

        if (bleMidi.isConnected() && bleMidi.sendMidi(blePacket, bleLength)) {
            blePacketsSent++;
        } else {
            blePacketsSkipped++;
        }

        markDisplayMidiEvent(data);
    }
};

UsbMidiInput usbMidi;

static void printStartupBanner()
{
    Serial.println();
    Serial.println("=== USB MIDI to BLE MIDI Bridge ===");
    Serial.println("Target: ESP32-S3 with USB-OTG host");
    Serial.print("BLE name: ");
    Serial.println(BLE_DEVICE_NAME);
    Serial.println("Connect your iPhone from an app's Bluetooth MIDI device menu.");
    Serial.println();
}

static void printDisplayLine(int16_t x, int16_t y, uint8_t size, uint16_t color, const char* text)
{
    if (!displayReady) {
        return;
    }

    display->setTextSize(size);
    display->setTextColor(color);
    display->setCursor(x, y);
    display->print(text);
}

static void initDisplay()
{
#if LCD_ENABLE_PIN >= 0
    pinMode(LCD_ENABLE_PIN, OUTPUT);
    digitalWrite(LCD_ENABLE_PIN, LOW);
#endif

#if LCD_BACKLIGHT_PIN >= 0
    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
#endif

    displayReady = display->begin(40000000);
    if (!displayReady) {
        Serial.println("[LCD] Display init failed.");
        return;
    }

    displayStaticDrawn = false;
    updateDisplayDashboard(true);
}

static void initBoardUsbHostPower()
{
#if USB_HOST_BOOST_EN_PIN >= 0
    pinMode(USB_HOST_BOOST_EN_PIN, OUTPUT);
    digitalWrite(USB_HOST_BOOST_EN_PIN, USB_HOST_POWER_FROM_BATTERY ? HIGH : LOW);
#endif

#if USB_HOST_VBUS_EN_PIN >= 0
    pinMode(USB_HOST_VBUS_EN_PIN, OUTPUT);
    digitalWrite(USB_HOST_VBUS_EN_PIN, USB_HOST_POWER_FROM_BATTERY ? LOW : HIGH);
#endif

#if USB_HOST_LIMIT_EN_PIN >= 0
    pinMode(USB_HOST_LIMIT_EN_PIN, OUTPUT);
    digitalWrite(USB_HOST_LIMIT_EN_PIN, HIGH);
#endif

#if USB_HOST_SEL_PIN >= 0
    pinMode(USB_HOST_SEL_PIN, OUTPUT);
    digitalWrite(USB_HOST_SEL_PIN, HIGH);
#endif

    delay(100);
}

static void updateDisplayStatus(bool usbConnected, bool bleConnected)
{
    (void)usbConnected;
    (void)bleConnected;
    displayRefreshPending = true;
}

static void markDisplayMidiEvent(const uint8_t* data)
{
    midiEventsSeen++;

    const uint8_t status = data[1];
    const uint8_t messageType = status & 0xF0;
    const uint8_t channel = (status & 0x0F) + 1;
    const uint8_t data1 = data[2];
    const uint8_t data2 = data[3];
    char noteBuffer[6] = {0};

    if ((messageType == 0x90 && data2 > 0) || messageType == 0x80 || (messageType == 0x90 && data2 == 0)) {
        const bool noteOn = (messageType == 0x90 && data2 > 0);
        noteEventsSeen++;

        if (data1 < 128) {
            if (noteOn && !activeNotes[data1]) {
                activeNotes[data1] = true;
                activeNotesCount++;
            } else if (!noteOn && activeNotes[data1]) {
                activeNotes[data1] = false;
                if (activeNotesCount > 0) {
                    activeNotesCount--;
                }
            }
        }

        snprintf(lastMidiText,
                 sizeof(lastMidiText),
                 "%s %s ch%u v%u",
                 noteOn ? "On" : "Off",
                 noteName(data1, noteBuffer, sizeof(noteBuffer)),
                 channel,
                 data2);
    } else if (messageType == 0xB0) {
        snprintf(lastMidiText, sizeof(lastMidiText), "CC %u=%u ch%u", data1, data2, channel);
    } else if (messageType == 0xE0) {
        const uint16_t bend = (data1 & 0x7F) | ((data2 & 0x7F) << 7);
        snprintf(lastMidiText, sizeof(lastMidiText), "Bend %u ch%u", bend, channel);
    } else {
        snprintf(lastMidiText,
                 sizeof(lastMidiText),
                 "%s ch%u %u %u",
                 statusName(status),
                 channel,
                 data1,
                 data2);
    }

    lastMidiMs = millis();
    displayRefreshPending = true;
}

static void printMetricLine(int16_t y, const char* label, const char* value, uint16_t valueColor)
{
    printDisplayLine(18, y, 1, RGB565_LIGHTGRAY, label);
    printDisplayLine(82, y, 1, valueColor, value);
}

static void drawStatusPill(int16_t x, int16_t y, const char* label, const char* value, bool ok)
{
    const uint16_t border = ok ? RGB565_LIME : RGB565_GOLD;
    const uint16_t fill = ok ? RGB565(0, 52, 28) : RGB565(64, 44, 0);

    display->fillRoundRect(x, y, 94, 42, 8, fill);
    display->drawRoundRect(x, y, 94, 42, 8, border);
    printDisplayLine(x + 8, y + 7, 1, RGB565_LIGHTGRAY, label);
    printDisplayLine(x + 8, y + 21, 2, border, value);
}

static void drawTypingCat(uint32_t nowMs, bool midiActive)
{
    const uint16_t fur = RGB565(226, 232, 232);
    const uint16_t line = RGB565(48, 56, 64);
    const uint16_t table = RGB565(96, 64, 40);
    const uint16_t drum = midiActive ? RGB565_LIME : RGB565_CYAN;
    const bool leftHit = midiActive && ((nowMs / 160) % 2 == 0);
    const bool rightHit = midiActive && !leftHit;
    const bool idleTap = !midiActive && ((nowMs / 900) % 2 == 0);
    const bool blink = ((nowMs / 2600) % 5) == 0;

    display->fillRect(18, 66, 204, 72, RGB565_BLACK);

    // Table layer.
    display->fillRoundRect(42, 116, 156, 16, 5, table);
    display->drawFastHLine(48, 119, 144, RGB565(160, 104, 56));

    // Body and head layer.
    display->fillRoundRect(62, 92, 64, 40, 18, fur);
    display->fillCircle(84, 82, 31, fur);
    display->fillTriangle(60, 66, 68, 43, 78, 67, fur);
    display->fillTriangle(90, 66, 103, 43, 108, 72, fur);
    display->drawCircle(84, 82, 31, line);
    display->drawLine(60, 66, 68, 43, line);
    display->drawLine(68, 43, 78, 67, line);
    display->drawLine(90, 66, 103, 43, line);
    display->drawLine(103, 43, 108, 72, line);

    if (blink) {
        display->drawLine(72, 80, 78, 80, RGB565_BLACK);
        display->drawLine(91, 80, 97, 80, RGB565_BLACK);
    } else {
        display->fillCircle(75, 80, 3, RGB565_BLACK);
        display->fillCircle(94, 80, 3, RGB565_BLACK);
    }
    display->fillCircle(84, 89, 3, RGB565_DARKGRAY);
    display->drawLine(84, 92, 78, 97, line);
    display->drawLine(84, 92, 90, 97, line);
    display->drawLine(57, 87, 43, 82, line);
    display->drawLine(58, 92, 42, 92, line);
    display->drawLine(58, 97, 43, 102, line);

    // Bongo layer.
    display->fillCircle(146, 109, 20, RGB565(32, 38, 48));
    display->fillCircle(184, 109, 20, RGB565(32, 38, 48));
    display->drawCircle(146, 109, 20, drum);
    display->drawCircle(184, 109, 20, drum);
    display->fillCircle(146, 109, 11, RGB565(20, 24, 32));
    display->fillCircle(184, 109, 11, RGB565(20, 24, 32));

    // Paw layer.
    display->fillRoundRect(116, (leftHit || idleTap) ? 103 : 94, 32, 13, 7, fur);
    display->fillRoundRect(168, rightHit ? 103 : 94, 32, 13, 7, fur);
    display->drawRoundRect(116, (leftHit || idleTap) ? 103 : 94, 32, 13, 7, line);
    display->drawRoundRect(168, rightHit ? 103 : 94, 32, 13, 7, line);

    if (leftHit) {
        display->drawCircle(146, 109, 24, RGB565_GOLD);
    }
    if (rightHit) {
        display->drawCircle(184, 109, 24, RGB565_GOLD);
    }

    printDisplayLine(128, 72, 1, midiActive ? RGB565_LIME : RGB565_LIGHTGRAY,
                     midiActive ? "bongo notes" : "idle");
}

static void updateDisplayDashboard(bool force)
{
    if (!displayReady) {
        return;
    }

    static uint32_t lastDrawMs = 0;
    if (!force && millis() - lastDrawMs < 750) {
        return;
    }

    lastDrawMs = millis();
    displayRefreshPending = false;

    const bool usbConnected = usbMidi.isConnected();
    const bool bleConnected = bleMidi.isConnected();
    const uint32_t nowMs = millis();
    const bool midiActive = lastMidiMs > 0 && nowMs - lastMidiMs < 700;

    if (!displayStaticDrawn || force) {
        display->fillScreen(RGB565_BLACK);
        display->fillRoundRect(6, 6, 228, 228, 10, RGB565(8, 16, 28));
        display->drawRoundRect(6, 6, 228, 228, 10, RGB565_CYAN);
        display->fillRoundRect(12, 12, 216, 216, 8, RGB565_BLACK);
        printDisplayLine(22, 20, 2, RGB565_CYAN, "BONGO MIDI");
        printDisplayLine(22, 44, 1, RGB565_GOLD, BLE_DEVICE_NAME);
        displayStaticDrawn = true;
    }

    drawTypingCat(nowMs, midiActive);
    display->fillRect(18, 140, 204, 84, RGB565_BLACK);
    drawStatusPill(20, 142, "Roland USB", usbConnected ? "OK" : "WAIT", usbConnected);
    drawStatusPill(126, 142, "iPad BLE", bleConnected ? "OK" : "READY", bleConnected);

    char value[48] = {0};
    snprintf(value, sizeof(value), "Notes %lu  Held %u", noteEventsSeen, activeNotesCount);
    printDisplayLine(22, 190, 1, noteEventsSeen > 0 ? RGB565_LIME : RGB565_LIGHTGRAY, value);

    snprintf(value, sizeof(value), "Sent %lu  Skip %lu", blePacketsSent, blePacketsSkipped);
    printDisplayLine(110, 190, 1, blePacketsSent > 0 ? RGB565_LIME : RGB565_LIGHTGRAY, value);

    printDisplayLine(22, 204, 1, lastMidiMs > 0 ? RGB565_WHITE : RGB565_DARKGRAY, lastMidiText);

    if (!usbConnected) {
        printDisplayLine(22, 218, 1, RGB565_GOLD,
                         usbMidi.getLastError().length() > 0 ? "Check Roland USB mode" : "Use HOST + power USB_DEV");
    } else if (midiEventsSeen == 0) {
        printDisplayLine(22, 218, 1, RGB565_GOLD, "Press keys to test");
    } else if (!bleConnected) {
        printDisplayLine(22, 218, 1, RGB565_GOLD, "Connect app to BLE");
    } else {
        printDisplayLine(22, 218, 1, RGB565_LIME, "MIDI is flowing");
    }
}

static void printStatusIfChanged()
{
    static bool lastUsbConnected = false;
    static bool lastBleConnected = false;
    static uint32_t lastSummaryMs = 0;

    const bool usbConnected = usbMidi.isConnected();
    const bool bleConnected = bleMidi.isConnected();

    if (usbConnected != lastUsbConnected || bleConnected != lastBleConnected) {
        Serial.printf("[STATUS] USB=%s BLE=%s\n",
                      usbConnected ? "connected" : "waiting",
                      bleConnected ? "connected" : "advertising");
        lastUsbConnected = usbConnected;
        lastBleConnected = bleConnected;
        updateDisplayStatus(usbConnected, bleConnected);
    }

    if (millis() - lastSummaryMs >= 10000) {
        lastSummaryMs = millis();
        Serial.printf("[STATS] usb=%lu ble_sent=%lu ble_skipped=%lu\n",
                      usbPacketsSeen,
                      blePacketsSent,
                      blePacketsSkipped);
        displayRefreshPending = true;
    }

    updateDisplayDashboard();
}

void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(1000);
    printStartupBanner();
    initDisplay();
    initBoardUsbHostPower();

    if (usbMidi.begin()) {
        Serial.println("[USB] Host initialized. Waiting for a class-compliant USB MIDI device...");
    } else {
        Serial.println("[USB] Host init failed: " + usbMidi.getLastError());
    }

    bleMidi.begin(BLE_DEVICE_NAME);
    Serial.println("[BLE] MIDI server advertising.");
}

void loop()
{
    usbMidi.task();
    bleMidi.task();
    printStatusIfChanged();
    delayMicroseconds(50);
}
