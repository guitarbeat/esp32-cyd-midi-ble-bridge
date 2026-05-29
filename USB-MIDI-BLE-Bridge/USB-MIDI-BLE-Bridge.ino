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
#include "bongo_cat/BongoCat.h"
#include "BridgeUi.h"
#include "BridgeSettings.h"
#include "RTPMidiConfig.h"
#include "NetworkServices.h"
#include "MidiBridge.h"
#include "MidiCodec.h"

#ifndef BLE_DEVICE_NAME_TEXT
#define BLE_DEVICE_NAME_TEXT "Piano BLE Bridge"
#endif

#ifndef ENABLE_BLE_TO_USB
#define ENABLE_BLE_TO_USB 0
#endif

#ifndef RTP_MIDI_SESSION_NAME
#define RTP_MIDI_SESSION_NAME BLE_DEVICE_NAME_TEXT
#endif

static const char* kDefaultBleName = BLE_DEVICE_NAME_TEXT;
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

static uint32_t midiEventsSeen = 0;
static uint32_t noteEventsSeen = 0;
static uint32_t blePacketsReceived = 0;
static uint32_t usbPacketsSent = 0;
static uint32_t usbPacketsSkipped = 0;
static bool displayReady = false;
static uint32_t lastMidiMs = 0;
static char lastMidiText[36] = "none";
static bool displayRefreshPending = true;
static bool displayStaticDrawn = false;
static BongoCatDisplay bongoCat;

static void updateDisplayDashboard(bool force = false);
static void markDisplayMidiEvent(const uint8_t* data);
static void markDisplayRawMidiEvent(const uint8_t* data, size_t length);

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
        displayRefreshPending = true;
    }

    void onMidiDataReceived(const uint8_t* data, size_t length) override
    {
        uint8_t midiPacket[4] = {0};
        const MidiBridge::Result result = midiBridge.forward(data, length, midiPacket);
        if (result == MidiBridge::Result::kForwarded) {
            markDisplayMidiEvent(midiPacket);
        }
    }
};

UsbMidiInput usbMidi;

#if ENABLE_BLE_TO_USB
static void onBleMidiReceived(const uint8_t* data, size_t length)
{
    blePacketsReceived++;
    if (usbMidi.sendMidiMessage(data, length)) {
        usbPacketsSent++;
    } else {
        usbPacketsSkipped++;
    }
    markDisplayRawMidiEvent(data, length);
    displayRefreshPending = true;
}
#endif

static void printStartupBanner()
{
    Serial.println();
    Serial.println("=== USB MIDI to BLE MIDI Bridge ===");
    Serial.println("Target: ESP32-S3 with USB-OTG host");
    Serial.print("BLE name: ");
    Serial.println(bridgeSettings.bleDeviceName());
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
#endif

    displayReady = display->begin(40000000);
    if (!displayReady) {
        Serial.println("[LCD] Display init failed.");
        return;
    }

    bridgeUi.begin(display, LCD_BACKLIGHT_PIN);
    bongoCat.begin();
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

static void markDisplayRawMidiEvent(const uint8_t* data, size_t length)
{
    uint8_t usbPacket[4] = {0, 0, 0, 0};
    if (length >= 1) {
        usbPacket[1] = data[0];
    }
    if (length >= 2) {
        usbPacket[2] = data[1];
    }
    if (length >= 3) {
        usbPacket[3] = data[2];
    }
    markDisplayMidiEvent(usbPacket);
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

        bridgeUi.onNoteEvent(noteOn, data1, data2);

        snprintf(lastMidiText,
                 sizeof(lastMidiText),
                 "%s %s ch%u v%u",
                 noteOn ? "On" : "Off",
                 MidiCodec::noteName(data1, noteBuffer, sizeof(noteBuffer)),
                 channel,
                 data2);
    } else if (messageType == 0xB0) {
        bridgeUi.onControlChange(data1, data2);
        snprintf(lastMidiText, sizeof(lastMidiText), "CC %u=%u ch%u", data1, data2, channel);
    } else if (messageType == 0xE0) {
        const uint16_t bend = (data1 & 0x7F) | ((data2 & 0x7F) << 7);
        snprintf(lastMidiText, sizeof(lastMidiText), "Bend %u ch%u", bend, channel);
    } else {
        snprintf(lastMidiText,
                 sizeof(lastMidiText),
                 "%s ch%u %u %u",
                 MidiCodec::statusName(status),
                 channel,
                 data1,
                 data2);
    }

    lastMidiMs = millis();
    bridgeUi.touchActivity(lastMidiMs);
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

static void tickBongoCat(uint32_t nowMs)
{
    if (!displayReady) {
        return;
    }

    static uint32_t lastCatDrawMs = 0;
    if (nowMs - lastCatDrawMs < 40) {
        return;
    }
    lastCatDrawMs = nowMs;

    const bool midiActive = lastMidiMs > 0 && nowMs - lastMidiMs < 700;
    bongoCat.update(nowMs, midiActive, noteEventsSeen);
    bongoCat.draw(display);
    bridgeUi.drawOverlays(nowMs);
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
    if (!displayStaticDrawn || force) {
        display->fillScreen(RGB565_BLACK);
        display->fillRoundRect(6, 6, 228, 228, 10, RGB565(8, 16, 28));
        display->drawRoundRect(6, 6, 228, 228, 10, RGB565_CYAN);
        display->fillRoundRect(12, 12, 216, 216, 8, RGB565_BLACK);
        printDisplayLine(22, 14, 2, RGB565_CYAN, "PIANO BLE");
        printDisplayLine(22, 36, 1, RGB565_GOLD, bridgeSettings.bleDeviceName());
        displayStaticDrawn = true;
    }

    if (!bridgeUi.shouldDrawStatusPanel()) {
        return;
    }

    const int16_t statusY = BongoCatDisplay::kStatusTop;
    display->fillRect(18, statusY, 204, 240 - statusY - 12, RGB565_BLACK);
    drawStatusPill(20, statusY + 8, "USB MIDI", usbConnected ? "OK" : "WAIT", usbConnected);
    drawStatusPill(126, statusY + 8, "BLE", bleConnected ? "OK" : "READY", bleConnected);

    if (usbConnected && usbMidi.getDeviceName().length() > 0) {
        char keyboardLine[40] = {0};
        snprintf(keyboardLine, sizeof(keyboardLine), "%.36s", usbMidi.getDeviceName().c_str());
        printDisplayLine(22, statusY + 36, 1, RGB565_WHITE, keyboardLine);
    }

    const MidiBridge::Counters& midiStats = midiBridge.counters();

    char value[48] = {0};
    if (bridgeUi.shouldDrawFullMetrics()) {
        snprintf(value, sizeof(value), "Notes %lu  Held %u", noteEventsSeen, bridgeUi.heldNoteCount());
        printDisplayLine(22, statusY + 52, 1, noteEventsSeen > 0 ? RGB565_LIME : RGB565_LIGHTGRAY, value);

        snprintf(value, sizeof(value), "%u/min  BLE %lu", bridgeUi.notesPerMinute(), midiStats.blePacketsSent);
        printDisplayLine(110, statusY + 52, 1, midiStats.blePacketsSent > 0 ? RGB565_LIME : RGB565_LIGHTGRAY, value);

#if ENABLE_BLE_TO_USB
        snprintf(value, sizeof(value), "In %lu out %lu/%lu", blePacketsReceived, usbPacketsSent, usbPacketsSkipped);
        printDisplayLine(22, statusY + 64, 1, usbPacketsSent > 0 ? RGB565_LIME : RGB565_LIGHTGRAY, value);
#else
        if (bridgeSettings.transposeSemitones() != 0) {
            snprintf(value, sizeof(value), "Transpose %+d", bridgeSettings.transposeSemitones());
            printDisplayLine(22, statusY + 64, 1, RGB565_GOLD, value);
        } else if (bridgeSettings.midiChannelFilter() > 0) {
            snprintf(value, sizeof(value), "MIDI ch%u only", bridgeSettings.midiChannelFilter());
            printDisplayLine(22, statusY + 64, 1, RGB565_GOLD, value);
#if ENABLE_RTP_MIDI
        } else if (networkServices.isSetupMode()) {
            char wifiLine[40] = {0};
            snprintf(wifiLine, sizeof(wifiLine), "Join %s", networkServices.setupApSsid());
            printDisplayLine(22, statusY + 64, 1, RGB565_GOLD, wifiLine);
        } else if (networkServices.isLanReady()) {
            char rtpLine[40] = {0};
#if ENABLE_OTA
            if (networkServices.isOtaActive()) {
                snprintf(rtpLine, sizeof(rtpLine), "OTA updating...");
            } else {
                snprintf(rtpLine, sizeof(rtpLine), "RTP %s", networkServices.localIpString());
            }
#else
            snprintf(rtpLine, sizeof(rtpLine), "RTP %s", networkServices.localIpString());
#endif
            printDisplayLine(22, statusY + 64, 1, networkServices.hasRtpSession() ? RGB565_LIME : RGB565_GOLD, rtpLine);
#endif
        } else {
            printDisplayLine(22, statusY + 64, 1, lastMidiMs > 0 ? RGB565_WHITE : RGB565_DARKGRAY, lastMidiText);
        }
#endif
    }

    if (!usbConnected) {
        printDisplayLine(22, statusY + 88, 1, RGB565_GOLD,
                         usbMidi.getLastError().length() > 0 ? "Check USB MIDI mode" : "Use HOST + power USB_DEV");
#if ENABLE_RTP_MIDI
    } else if (networkServices.isSetupMode()) {
        printDisplayLine(22, statusY + 88, 1, RGB565_GOLD, "Open http://192.168.4.1");
#endif
    } else if (midiEventsSeen == 0) {
        printDisplayLine(22, statusY + 88, 1, RGB565_GOLD, "Press keys to test");
    } else if (!bleConnected) {
        printDisplayLine(22, statusY + 88, 1, RGB565_GOLD, "Connect app to BLE");
#if ENABLE_BLE_TO_USB
    } else if (!usbMidi.canSend()) {
        printDisplayLine(22, statusY + 88, 1, RGB565_GOLD, "USB IN only (no device OUT)");
    } else if (blePacketsReceived == 0 && midiStats.blePacketsSent > 0) {
        printDisplayLine(22, statusY + 88, 1, RGB565_LIME, "USB -> BLE OK");
    } else if (usbPacketsSent > 0) {
        printDisplayLine(22, statusY + 88, 1, RGB565_LIME, "Two-way MIDI active");
#endif
    } else {
        printDisplayLine(22, statusY + 88, 1, RGB565_LIME, "MIDI is flowing");
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
        const MidiBridge::Counters& midiStats = midiBridge.counters();
#if ENABLE_BLE_TO_USB
        Serial.printf("[STATS] usb=%lu ble_sent=%lu ble_skipped=%lu ble_in=%lu usb_out=%lu/%lu can_send=%s\n",
                      midiStats.usbPacketsSeen,
                      midiStats.blePacketsSent,
                      midiStats.blePacketsSkipped,
                      blePacketsReceived,
                      usbPacketsSent,
                      usbPacketsSkipped,
                      usbMidi.canSend() ? "yes" : "no");
#else
        Serial.printf("[STATS] usb=%lu ble_sent=%lu ble_skipped=%lu\n",
                      midiStats.usbPacketsSeen,
                      midiStats.blePacketsSent,
                      midiStats.blePacketsSkipped);
#endif
        displayRefreshPending = true;
    }

    updateDisplayDashboard();
}

void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(1000);

    bridgeSettings.begin(kDefaultBleName);
    bridgeUi.applySavedDisplayMode(bridgeSettings.displayModeIndex());

    printStartupBanner();
    bridgeSettings.printSummary();
    initDisplay();
    initBoardUsbHostPower();

    if (usbMidi.begin()) {
        Serial.println("[USB] Host initialized. Waiting for a class-compliant USB MIDI device...");
    } else {
        Serial.println("[USB] Host init failed: " + usbMidi.getLastError());
    }

    bleMidi.begin(bridgeSettings.bleDeviceName());
    bridgeUi.setBle(&bleMidi);
    midiBridge.begin(&bleMidi, &bridgeSettings, &bridgeUi);
#if ENABLE_RTP_MIDI
    networkServices.begin(RTP_MIDI_SESSION_NAME, bridgeSettings.bleDeviceName());
    midiBridge.setNetwork(&networkServices);
#endif
#if ENABLE_BLE_TO_USB
    bleMidi.setMidiMessageCallback(onBleMidiReceived);
#endif
    Serial.println("[BLE] MIDI server advertising.");
}

void loop()
{
    usbMidi.task();
    bleMidi.task();
#if ENABLE_RTP_MIDI
    networkServices.task();
#endif
    const uint32_t nowMs = millis();
    bridgeUi.tick(nowMs);
    tickBongoCat(nowMs);
    printStatusIfChanged();
    delayMicroseconds(50);
}
