# Classic ESP32/CYD USB MIDI to BLE MIDI Fallback

Your webflasher log shows:

```text
Chip type ESP32
Auto-detected Flash size: 4MB
```

That confirms a classic ESP32, not ESP32-S3. The board can run BLE MIDI, but its built-in USB connector is only for serial flashing/logging and cannot act as a USB host for a MIDI keyboard.

This fallback sketch uses an external MAX3421E USB host module:

```text
USB MIDI keyboard -> MAX3421E USB host -> classic ESP32/CYD -> BLE MIDI -> iPhone
```

## Required Libraries

Install these in Arduino IDE Library Manager:

- `USB Host Shield Library 2.0`

The sketch folder includes its own BLE helper files so it can be opened directly in Arduino IDE.

## Wiring Notes

The exact pins depend on your MAX3421E module and which CYD header pins are exposed. The USB Host Shield 2.0 library uses its ESP32 pin defaults unless you customize the library settings.

Common ESP32 wiring for MAX3421E modules is:

```text
MAX3421E SCK  -> ESP32 GPIO18
MAX3421E MISO -> ESP32 GPIO19
MAX3421E MOSI -> ESP32 GPIO23
MAX3421E SS   -> ESP32 GPIO5
MAX3421E INT  -> ESP32 GPIO17
MAX3421E GND  -> ESP32 GND
MAX3421E VCC  -> module-appropriate 3.3V/5V input
USB VBUS      -> powered 5V for the keyboard
```

Many CYD boards do not expose every convenient SPI pin, so verify your header/pinout before soldering. If those pins are unavailable, the cleaner path is still an ESP32-S3 board with native USB-OTG.

## Arduino IDE Settings

1. Select a classic ESP32 target, usually `ESP32 Dev Module` or the matching CYD board entry.
2. Open `Classic-ESP32-MAX3421E-MIDI-BLE.ino`.
3. Upload at `115200` or your normal ESP32 upload speed.
4. Open Serial Monitor at `115200`.
5. Confirm `MAX3421E initialized`.
6. Connect the USB MIDI keyboard to the MAX3421E host port.
7. Connect from iPhone in an app's Bluetooth MIDI device menu to `CYD MIDI Bridge`.

## Important

This sketch does not make the built-in USB port host-capable. It only works with a real external USB host controller.
