# Build Notes

## Tooling Installed

- Arduino CLI `1.5.0`
- PlatformIO Core `6.1.19`
- Arduino ESP32 core `3.3.8`
- Arduino libraries:
  - `USB Host Shield Library 2.0` `1.7.0`
  - `FastLED` `3.10.3`

## Verified Arduino CLI Builds

Classic ESP32/CYD with external MAX3421E USB host module:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 .\Classic-ESP32-MAX3421E-MIDI-BLE
```

ESP32-S3 with native USB-OTG host:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32s3 .\USB-MIDI-BLE-Bridge
```

Both builds passed locally after installing the ESP32 Arduino core and dependencies.

## Hardware Reality Check

The observed webflasher output reports `Chip type ESP32`, which is a classic ESP32. It can run BLE MIDI, but it cannot directly host a USB MIDI keyboard from its onboard USB/serial connector. Use either:

- the classic ESP32 sketch with an external MAX3421E USB host module, or
- the ESP32-S3 sketch with a board that exposes native USB-OTG host.
