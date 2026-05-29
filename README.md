# Piano BLE Bridge

Piano BLE Bridge turns a USB MIDI digital piano, keyboard, or controller into a
Bluetooth LE MIDI instrument using inexpensive ESP32 hardware.

```text
USB MIDI piano -> ESP32 bridge -> Bluetooth LE MIDI -> iPad, iPhone, Mac, Android, DAW
```

Use it with GarageBand, MIDI games, DAWs, or any app that supports Bluetooth LE MIDI.

## Why I Built This

I wanted to send MIDI from my Roland F-20 digital piano to my iPad (GarageBand)
wirelessly, the same job done by commercial dongles like the
[DoReMiDi UTB-21 Pro](https://www.doremidi.cn/h-pd-25.html) (USB MIDI to Bluetooth
MIDI adapter). This project is an open, hackable replacement for that device:

- Same core job: USB MIDI host IN → Bluetooth LE MIDI out, no computer in the middle.
- Cheap, common hardware (ESP32-S3-USB-OTG) instead of a sealed dongle.
- A real display: connection status, link health, a mini keyboard, and Bongo Cat.
- Settings you can change on the device (transpose, channel filter) and that persist.
- WiFi **RTP-MIDI** (Apple MIDI) alongside BLE — join the board’s setup WiFi on first use; see [BUILD.md](BUILD.md).
- Fully open firmware you can read, modify, and reflash.

## What Works

- USB MIDI input from class-compliant digital pianos and MIDI controllers.
- Bluetooth LE MIDI output to iOS, iPadOS, macOS, Android, and desktop apps.
- **WiFi RTP-MIDI** (Apple MIDI) alongside BLE — captive-portal setup on first boot; see [BUILD.md](BUILD.md).
- **OTA updates** over WiFi when on your LAN — no USB cable for routine firmware flashes; see [BUILD.md](BUILD.md).
- A visible startup/status screen on the Espressif ESP32-S3-USB-OTG board.
- **Bongo Cat** sprite animation (from [vostoklabs/bongo_cat_monitor](https://github.com/vostoklabs/bongo_cat_monitor)) driven by your playing.
- On-screen **mini keyboard**, **velocity bar**, **sustain** indicator, and **notes/min** counter.
- **Board buttons** (ESP32-S3-USB-OTG): UP/DOWN transpose, MENU cycles channel filter, MENU hold cycles backlight dim, OK cycles display mode, OK hold panic/pause.
- **Backlight dim** after 90 s idle (configurable via MENU hold); wakes on MIDI activity.
- **NVS settings**: transpose, MIDI channel filter, display mode, backlight timeout (saved across reboots).
- **Link health** on Full display: USB→BLE forwarding latency (when connected).
- **Stage display mode**: cat-focused view with status hidden.
- Reproducible Arduino CLI builds and prebuilt firmware binaries from CI.

## Current Limits

- One-way by default: piano → BLE (USB host IN → BLE notify).
- Optional compile-time BLE → USB (see [BUILD.md](BUILD.md)); most digital pianos
  are receive-only over USB, so this rarely applies.
- Bluetooth audio is not supported.
- Some keyboards need 5 V VBUS on the host port before they enumerate.
- BLE MIDI latency depends on the receiving device and app.

## MIDI 2.0

Short version: this bridge speaks MIDI 1.0 today, and that is the right choice for
the F-20 → iPad use case.

- As of 2026 there is no finalized BLE MIDI 2.0 transport. Apps like GarageBand
  connect to this bridge as BLE MIDI 1.0; there is no standard way to carry MIDI 2.0
  over Bluetooth that they would understand yet.
- The Roland F-20 (and most current pianos) are MIDI 1.0 sources. MIDI 2.0's benefits
  (16-bit velocity, 32-bit controllers) cannot be invented from a 7-bit 1.0 source.

### Roadmap: USB MIDI 2.0 host ingest

A future enhancement is to host native USB MIDI 2.0 devices and parse Universal MIDI
Packets (UMP) directly. The ESP32-S3 can do this (see the UMP host work in
[sauloverissimo/ESP32_Host_MIDI](https://github.com/sauloverissimo/ESP32_Host_MIDI)).
Until BLE MIDI 2.0 ships, such input would still down-scale to BLE MIDI 1.0 on output,
so this is tracked as forward-looking, not a current feature.

## Recommended Hardware

### Primary: ESP32-S3 with native USB host

The supported product firmware is **[USB-MIDI-BLE-Bridge](USB-MIDI-BLE-Bridge/)** for an
ESP32-S3 board with USB-OTG host. This repo is tested on the official Espressif
**ESP32-S3-USB-OTG** development board (display + Type-A host + `USB_DEV` power).

Wiring for that board:

- Micro-USB **USB-to-UART** — flash firmware and serial logs.
- Type-A **USB HOST** — piano or MIDI controller.
- **USB_DEV** — 5 V power so the host port can supply VBUS to the keyboard.

### Fallback: Classic ESP32 + MAX3421E

Only if you already have a **classic ESP32** (not S3): add a MAX3421E USB host
shield/module and build **[Classic-ESP32-MAX3421E-MIDI-BLE](Classic-ESP32-MAX3421E-MIDI-BLE/)**.
Classic ESP32 boards cannot host USB MIDI from their onboard USB connector alone.

## Quick Start

1. **Get hardware** — ESP32-S3-USB-OTG (recommended) or classic ESP32 + MAX3421E.
2. **Flash firmware** — download the latest `USB-MIDI-BLE-Bridge` `.bin` from the
   [GitHub Actions](https://github.com/guitarbeat/esp32-cyd-midi-ble-bridge/actions)
   workflow artifacts, or build with Arduino CLI (see [BUILD.md](BUILD.md)).
3. **Wire the piano** — keyboard → Type-A host; board powered (including `USB_DEV` on the OTG board).
4. **Pair in your app** — open GarageBand (or your MIDI app) → Bluetooth MIDI devices → connect to **Piano BLE Bridge**.
5. **Play** — the display should show USB OK and BLE connected; note events should appear in the app.

## Bluetooth Name

Default BLE name: **Piano BLE Bridge**

Change it by editing `BLE_DEVICE_NAME_TEXT` in the sketch or passing a build flag
(see [BUILD.md](BUILD.md)).

## Build From Source

Contributor-oriented steps live in [BUILD.md](BUILD.md). Short version:

```bash
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "USB Host Shield Library 2.0" "GFX Library for Arduino"
arduino-cli compile \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc' \
  ./USB-MIDI-BLE-Bridge
```

## Pairing

1. Flash the firmware for your board (S3 product sketch or classic fallback).
2. Plug the piano into the ESP32 **host** port.
3. Power the bridge (see hardware section for VBUS).
4. In your MIDI app, open the **Bluetooth MIDI** device list (not always the system Bluetooth settings on iOS).
5. Connect to **Piano BLE Bridge** and play a few keys.

## Troubleshooting

### The Piano Does Not Connect Over USB

- Confirm the instrument is **class-compliant USB MIDI**.
- Confirm **5 V VBUS** on the host port. On ESP32-S3-USB-OTG, Micro-USB debug power alone
  can boot BLE while the Type-A port has no power for the keyboard — use **USB HOST**
  for the piano and **USB_DEV** for 5 V.
- Try another data-capable USB cable.
- Check the on-screen hint: `Use HOST + power USB_DEV`.

### Bluetooth Connects But No Notes in the App

- If the display shows **USB WAIT**, the keyboard is not enumerated — fix power/cable first.
- If **USB OK** but note counters stay at 0, check the piano’s USB mode (often “Generic” / class-compliant MIDI).
- If USB counters increase but the app is silent, check the app’s **BLE MIDI input** routing.

### Unplugging the Piano

When the USB keyboard is removed, the bridge **stays running** and waits for you to plug
the piano back in. Bluetooth MIDI should remain connected in your app — you usually do not
need to reconnect BLE after replugging. If MIDI does not resume, replug the piano or restart
the app’s BLE MIDI session.

### No Bluetooth MIDI Device Appears

- Confirm the firmware booted (display shows **Piano BLE Bridge** on the OTG board).
- Use the MIDI app’s Bluetooth MIDI menu, not only system Bluetooth on iOS.
- Restart the board after changing the BLE name.

### Classic ESP32 Board Does Not See USB MIDI

A classic ESP32 cannot USB-host from its built-in connector. Use the MAX3421E fallback
sketch or switch to an ESP32-S3 native USB host board.

## Upstream Reference

USB host and BLE patterns are inspired by
[sauloverissimo/ESP32_Host_MIDI](https://github.com/sauloverissimo/ESP32_Host_MIDI)
(MIT). This repo is a **single-purpose bridge**, not a copy of that multi-transport library.

## Development

GitHub Actions compile both supported sketches on every push and publish an S3
firmware artifact. See [BUILD.md](BUILD.md) for FQBNs, pins, and recovery.

## Credits

ESP32 USB MIDI host work by [sauloverissimo](https://github.com/sauloverissimo/ESP32_Host_MIDI);
bridge and display work by Liam Jones.

## License

MIT. See [LICENSE.txt](LICENSE.txt).
