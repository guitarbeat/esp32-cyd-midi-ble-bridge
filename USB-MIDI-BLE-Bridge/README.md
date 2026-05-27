# USB MIDI to BLE MIDI Bridge

This sketch turns an ESP32-S3 board with native USB-OTG into a one-way MIDI bridge:

```text
USB MIDI keyboard -> ESP32-S3 -> BLE MIDI -> iPhone/iPad
```

It is intentionally guarded for ESP32-S3 only. A classic CYD such as an ESP32-2432S024/ESP32-2432S028 with an ESP32-WROOM-32 module cannot host a USB MIDI keyboard through firmware alone.

## Board Check

- `ESP32-WROOM-32`, `ESP32-2432S024`, or `ESP32-2432S028`: classic CYD. This sketch will not compile for it because the chip has no native USB host peripheral.
- `ESP32-S3`, `ESP-WROOM-S3`, or `ESP32-S3-2432...`: likely usable if the board exposes the USB-OTG D+/D- lines and can power the keyboard.
- Classic CYD fallback requires an external USB host controller, for example a MAX3421E module. That needs a separate driver path and is not this firmware.

## Hardware

- ESP32-S3 board with USB-OTG host support.
- USB OTG adapter/cable for the MIDI keyboard.
- A class-compliant USB MIDI keyboard or controller.
- Optional separate USB cable/port for flashing and serial logs, depending on the board.

## Arduino IDE Settings

1. Install `esp32 by Espressif Systems` in Boards Manager.
2. Select an ESP32-S3 board, usually `ESP32S3 Dev Module`.
3. Set `Tools > USB Mode` to `USB Host`.
4. Set PSRAM/flash options to match your board.
5. Open `USB-MIDI-BLE-Bridge.ino` and upload.

The sketch folder includes its own `USBConnection` and `BLEConnection` helper files so it can be opened directly in Arduino IDE.

## Test

1. Open Serial Monitor at `115200`.
2. Confirm `USB Host initialized`.
3. Connect the keyboard to the ESP32-S3 OTG port.
4. Confirm `MIDI device connected`.
5. Open GarageBand, AUM, or another iOS MIDI app.
6. Use the app's Bluetooth MIDI device menu and connect to `CYD MIDI Bridge`.
7. Press keys and confirm the app receives notes.

The serial log prints USB/BLE connection state and basic MIDI event details.
