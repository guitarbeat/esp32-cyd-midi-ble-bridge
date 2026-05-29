# Build Notes

## Tooling

- Arduino CLI (tested with 1.5.x)
- Arduino ESP32 core 3.3.x (`esp32:esp32`)
- Libraries:
  - `USB Host Shield Library 2.0` (classic MAX3421E fallback only)
  - `GFX Library for Arduino` (ESP32-S3 display build only)

## Product Firmware (ESP32-S3)

Official Espressif ESP32-S3-USB-OTG board — 8 MB flash, USB CDC on boot:

```bash
arduino-cli compile \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc' \
  ./USB-MIDI-BLE-Bridge

arduino-cli upload \
  -p /dev/cu.usbmodem11101 \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc' \
  ./USB-MIDI-BLE-Bridge
```

Replace the port with your board (`arduino-cli board list`).

### Prebuilt binary

CI builds `./USB-MIDI-BLE-Bridge` and uploads `USB-MIDI-BLE-Bridge.ino.bin` as a
workflow artifact on each push. Download it from the Actions tab for your branch.

Flash with `esptool.py` or the Arduino IDE “Flash from file” if you use a third-party
flasher; match the same board/partition settings as above.

### Optional: BLE → USB reverse path

Default build is **one-way** (USB IN → BLE). To enable sending MIDI from a BLE app
back to the USB device (only if the device exposes a USB MIDI OUT endpoint):

```bash
arduino-cli compile \
  --build-property 'build.extra_flags=-DENABLE_BLE_TO_USB=1' \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc' \
  ./USB-MIDI-BLE-Bridge
```

### Optional: USB debug logging

```bash
arduino-cli compile \
  --build-property 'build.extra_flags=-DDEBUG_USB=1' \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc' \
  ./USB-MIDI-BLE-Bridge
```

### On-board controls (ESP32-S3-USB-OTG)

| Button | GPIO | Action |
|--------|------|--------|
| **UP+** | 10 | Transpose +1 semitone |
| **DW-** | 11 | Transpose −1 semitone |
| **MENU** | 14 | Tap: cycle MIDI channel filter (all → ch1…ch16) |
| **MENU** (hold ~1 s) | 14 | Cycle backlight dim timeout (30s / 90s / 3m / never) |
| **MENU** (hold ~4 s) | 14 | Open WiFi setup AP (captive portal) |
| **OK** / Boot | 0 | Tap: cycle display mode (Full → Performance → Minimal → Stage) |
| **OK** (hold ~1 s) | 0 | Send **All Notes Off** on BLE (panic) |
| **OK** (hold ~2.5 s) | 0 | Pause / resume USB→BLE forwarding |

A short toast appears at the bottom of the display when a setting changes.

On boards without the side buttons, set unused pins to `-1` via build flags
(e.g. `-DBOARD_BTN_UP=-1`).

Settings persist in NVS. Changing the BLE name in NVS requires a reboot to take effect (edit via reflash with `BLE_DEVICE_NAME_TEXT` or future tooling).

Unplugging the USB keyboard no longer reboots the board — plug the piano back in; BLE MIDI should stay connected in your app.

Backlight dims after the configured idle period; any MIDI activity wakes it.

### Custom BLE name

```bash
arduino-cli compile \
  --build-property 'build.extra_flags=-DBLE_DEVICE_NAME_TEXT=\"My Piano Bridge\"' \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc' \
  ./USB-MIDI-BLE-Bridge
```

### WiFi RTP-MIDI (Apple MIDI)

Enabled by default. Mirrors USB MIDI to **RTP-MIDI on port 5004** while BLE stays active (same LAN as your Mac). Uses the [AppleMIDI](https://github.com/lathoub/Arduino-AppleMidi-Library) library.

1. Install: `arduino-cli lib install AppleMIDI`
2. Flash firmware (RTP is on unless you set `ENABLE_RTP_MIDI` to `0` in `RTPMidiConfig.h`).
3. **First-time WiFi setup** (no credentials saved yet, or saved network unreachable):
   - The board opens a setup WiFi network named **`Piano-BLE-Bridge-Setup`** (spaces in the BLE name become dashes).
   - On your phone or laptop, join that network (open / no password).
   - A captive portal should open; if not, browse to **http://192.168.4.1**
   - Pick your home WiFi, enter the password, and tap **Save and connect**. The board reboots and joins your LAN.
4. **Re-open setup later:** hold **MENU** for ~4 seconds on the ESP32-S3-USB-OTG board.
5. On macOS: **Audio MIDI Setup → MIDI Studio → Network** — add a session with the bridge’s IP (Full display shows `RTP x.x.x.x` when Wi-Fi is up) and port **5004**.

Optional compile-time fallback (skips the portal on first boot if NVS is empty): copy `wifi_secrets.example.h` to `wifi_secrets.h` and set `WIFI_SSID_TEXT` / `WIFI_PASSWORD_TEXT` before building.

Wi-Fi uses DHCP. RTP forwards only after a host connects to the RTP session.

To disable RTP-MIDI (BLE-only build), set `#define ENABLE_RTP_MIDI 0` in `RTPMidiConfig.h`.

### Over-the-air (OTA) firmware updates

When the board is on your WiFi (after RTP setup), you can flash new firmware **without USB**:

1. Board and computer must be on the **same LAN**.
2. Serial log on boot prints `[OTA] Ready at piano-ble-bridge.local (port 3232)` (hostname derived from the BLE name).
3. Upload from this repo:

```bash
# By mDNS hostname (macOS usually resolves .local)
arduino-cli upload \
  -p piano-ble-bridge.local \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc' \
  ./USB-MIDI-BLE-Bridge

# Or use the IP shown on the display (RTP x.x.x.x line)
arduino-cli upload \
  -p 192.168.1.42 \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc' \
  ./USB-MIDI-BLE-Bridge
```

4. `arduino-cli board list` may also show a **network port** for the ESP32 when OTA is advertising.

Optional OTA password: copy `ota_secrets.example.h` to `ota_secrets.h` and set `OTA_PASSWORD_TEXT`, then pass
`--upload-property upload.password=YourPassword` on upload.

Disable OTA with `#define ENABLE_OTA 0` in `RTPMidiConfig.h` (RTP/BLE still work).

**Note:** The first flash after adding OTA still needs USB once. After that, use OTA for routine updates.

## Fallback Firmware (Classic ESP32 + MAX3421E)

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 ./Classic-ESP32-MAX3421E-MIDI-BLE
```

Requires the USB Host Shield library and wired MAX3421E module.

## Recovery (ESP32-S3-USB-OTG)

If the board disappears from `/dev/cu.*` after USB host firmware runs:

1. Hold **BOOT**
2. Press and release **RESET**
3. Release **BOOT**
4. Flash again using the **USB-to-UART** Micro-USB port

## USB host power pins (ESP32-S3-USB-OTG)

The sketch drives these before starting USB host:

| GPIO | Role |
| --- | --- |
| 18 | High = Type-A host mux selected |
| 12 | High = host VBUS from USB_DEV power path |
| 17 | High = current-limited host power switch |
| 13 | Low = battery boost disabled (default) |

Micro-USB can power the MCU and display while the Type-A port still needs VBUS from
**USB_DEV** or battery for many keyboards.

## Hardware reality check

Web flasher or serial output showing `Chip type: ESP32` (classic) means you need the
MAX3421E fallback sketch, not the S3 native USB host sketch.
