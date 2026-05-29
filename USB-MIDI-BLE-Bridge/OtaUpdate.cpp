#include "OtaUpdate.h"

#if ENABLE_RTP_MIDI && ENABLE_OTA

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#if __has_include("ota_secrets.h")
#include "ota_secrets.h"
#endif

#ifndef OTA_PASSWORD_TEXT
#define OTA_PASSWORD_TEXT ""
#endif

OtaUpdate otaUpdate;

void OtaUpdate::sanitizeHostname(const char* input, char* output, size_t outputSize)
{
    if (outputSize == 0) {
        return;
    }

    output[0] = '\0';
    if (input == nullptr || input[0] == '\0') {
        strncpy(output, "piano-ble-bridge", outputSize - 1);
        output[outputSize - 1] = '\0';
        return;
    }

    size_t outIndex = 0;
    for (size_t i = 0; input[i] != '\0' && outIndex < outputSize - 1; i++) {
        char ch = input[i];
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            output[outIndex++] = ch;
        } else if (ch == ' ' || ch == '_' || ch == '-') {
            if (outIndex > 0 && output[outIndex - 1] != '-') {
                output[outIndex++] = '-';
            }
        }
    }

    while (outIndex > 0 && output[outIndex - 1] == '-') {
        outIndex--;
    }

    if (outIndex == 0) {
        strncpy(output, "piano-ble-bridge", outputSize - 1);
        outIndex = strlen(output);
    }

    output[outIndex] = '\0';
}

void OtaUpdate::begin(const char* hostname)
{
    sanitizeHostname(hostname, hostname_, sizeof(hostname_));
    started_ = false;
    active_ = false;
}

void OtaUpdate::startIfNeeded()
{
    if (started_) {
        return;
    }

    if (!MDNS.begin(hostname_)) {
        Serial.println("[OTA] mDNS start failed");
    }

    ArduinoOTA.setHostname(hostname_);

    if (OTA_PASSWORD_TEXT[0] != '\0') {
        ArduinoOTA.setPassword(OTA_PASSWORD_TEXT);
    }

    ArduinoOTA.onStart([this]() {
        active_ = true;
        Serial.println("[OTA] Update started");
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("[OTA] Update complete — rebooting");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress %u%%\r", (progress * 100) / total);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error %u\n", error);
    });

    ArduinoOTA.begin();
    started_ = true;
    Serial.printf("[OTA] Ready at %s.local (port 3232)\n", hostname_);
    if (OTA_PASSWORD_TEXT[0] != '\0') {
        Serial.println("[OTA] Password required for upload");
    }
}

void OtaUpdate::task(bool wifiReady)
{
    if (!wifiReady) {
        return;
    }

    startIfNeeded();
    ArduinoOTA.handle();
}

#else

OtaUpdate otaUpdate;

void OtaUpdate::begin(const char* hostname)
{
    (void)hostname;
}

void OtaUpdate::task(bool wifiReady)
{
    (void)wifiReady;
}

#endif
