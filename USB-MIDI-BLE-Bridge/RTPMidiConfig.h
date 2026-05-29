#ifndef RTP_MIDI_CONFIG_H
#define RTP_MIDI_CONFIG_H

// RTP-MIDI (Apple MIDI) is enabled by default. WiFi credentials are saved via the setup AP (see BUILD.md).
// OTA firmware updates over WiFi are enabled when RTP is on (ENABLE_OTA, default 1).
// Optional compile-time fallback: copy wifi_secrets.example.h to wifi_secrets.h.
#define ENABLE_RTP_MIDI 1
#define ENABLE_OTA 1

#if ENABLE_RTP_MIDI
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif
#ifndef WIFI_SSID_TEXT
#define WIFI_SSID_TEXT ""
#endif
#ifndef WIFI_PASSWORD_TEXT
#define WIFI_PASSWORD_TEXT ""
#endif
#endif

#endif
