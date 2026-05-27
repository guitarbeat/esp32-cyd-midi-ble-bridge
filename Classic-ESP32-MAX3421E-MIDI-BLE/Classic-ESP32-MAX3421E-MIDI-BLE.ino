// USB MIDI to BLE MIDI bridge for classic ESP32 boards using an external
// MAX3421E USB host module/shield.
//
// Direction: USB MIDI keyboard/controller -> MAX3421E -> ESP32 -> BLE MIDI.
// This is the viable path for classic ESP32/CYD boards. The built-in USB
// connector on these boards is only for serial flashing/logging, not USB host.

#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "This fallback sketch targets classic ESP32 boards. Use USB-MIDI-BLE-Bridge for ESP32-S3 native USB-OTG boards."
#endif

#include <Usb.h>
#include <usbh_midi.h>
#include <usbhub.h>

#include "BLEConnection.h"

static const char* BLE_DEVICE_NAME = "CYD MIDI Bridge";
static const uint32_t SERIAL_BAUD = 115200;

USB Usb;
USBHub Hub(&Usb);
USBH_MIDI Midi(&Usb);
BLEConnection bleMidi;

static uint32_t midiMessagesSeen = 0;
static uint32_t bleMessagesSent = 0;
static uint32_t bleMessagesSkipped = 0;

static bool buildBleMidiPacket(const uint8_t* midiData, uint8_t midiLength, uint8_t* blePacket, size_t* bleLength)
{
    if (midiData == nullptr || blePacket == nullptr || bleLength == nullptr) {
        return false;
    }

    if (midiLength == 0 || midiLength > 3 || midiData[0] == 0x00) {
        return false;
    }

    const uint16_t timestamp = millis() & 0x1FFF;
    blePacket[0] = 0x80 | ((timestamp >> 7) & 0x3F);
    blePacket[1] = 0x80 | (timestamp & 0x7F);

    for (uint8_t i = 0; i < midiLength; i++) {
        blePacket[2 + i] = midiData[i];
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

static void pollUsbMidi()
{
    uint8_t midiData[3] = {0};
    uint8_t midiLength = 0;

    do {
        midiLength = Midi.RecvData(midiData);
        if (midiLength == 0) {
            return;
        }

        midiMessagesSeen++;

        uint8_t blePacket[5] = {0};
        size_t bleLength = 0;

        if (!buildBleMidiPacket(midiData, midiLength, blePacket, &bleLength)) {
            Serial.printf("[USBH] Ignored MIDI len=%u status=%02X\n", midiLength, midiData[0]);
            continue;
        }

        Serial.printf("[USBH] %-15s ch=%u data1=%3u data2=%3u BLE=%s\n",
                      statusName(midiData[0]),
                      ((midiData[0] & 0x0F) + 1),
                      midiLength > 1 ? midiData[1] : 0,
                      midiLength > 2 ? midiData[2] : 0,
                      bleMidi.isConnected() ? "connected" : "waiting");

        if (bleMidi.isConnected() && bleMidi.sendMidi(blePacket, bleLength)) {
            bleMessagesSent++;
        } else {
            bleMessagesSkipped++;
        }
    } while (midiLength > 0);
}

static void printStartupBanner()
{
    Serial.println();
    Serial.println("=== Classic ESP32 MAX3421E USB MIDI to BLE MIDI Bridge ===");
    Serial.println("Target: classic ESP32 plus external MAX3421E USB host module");
    Serial.print("BLE name: ");
    Serial.println(BLE_DEVICE_NAME);
    Serial.println("The board's built-in USB connector remains serial/flashing only.");
    Serial.println();
}

static void printStatus()
{
    static bool lastBleConnected = false;
    static uint8_t lastUsbState = 0xFF;
    static uint32_t lastSummaryMs = 0;

    const bool bleConnected = bleMidi.isConnected();
    const uint8_t usbState = Usb.getUsbTaskState();

    if (bleConnected != lastBleConnected || usbState != lastUsbState) {
        Serial.printf("[STATUS] USBH_STATE=0x%02X BLE=%s\n",
                      usbState,
                      bleConnected ? "connected" : "advertising");
        lastBleConnected = bleConnected;
        lastUsbState = usbState;
    }

    if (millis() - lastSummaryMs >= 10000) {
        lastSummaryMs = millis();
        Serial.printf("[STATS] midi=%lu ble_sent=%lu ble_skipped=%lu\n",
                      midiMessagesSeen,
                      bleMessagesSent,
                      bleMessagesSkipped);
    }
}

void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(1000);
    printStartupBanner();

    if (Usb.Init() == -1) {
        Serial.println("[USBH] MAX3421E initialization failed. Check SPI wiring, power, CS, and INT.");
    } else {
        Serial.println("[USBH] MAX3421E initialized. Connect a class-compliant USB MIDI device.");
    }

    bleMidi.begin(BLE_DEVICE_NAME);
    Serial.println("[BLE] MIDI server advertising.");
}

void loop()
{
    Usb.Task();

    if (Usb.getUsbTaskState() == USB_STATE_RUNNING) {
        pollUsbMidi();
    }

    bleMidi.task();
    printStatus();
    delay(1);
}
