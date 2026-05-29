#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <Arduino.h>
#include "RTPMidiConfig.h"

class WifiProvisioning {
public:
    bool begin(const char* setupApBaseName);
    void task();
    void enterSetupMode();

    bool isConnected() const;
    bool isSetupMode() const;
    const char* localIpString() const;
    const char* setupApSsid() const;

private:
#if ENABLE_RTP_MIDI
    enum class State : uint8_t { kSetup, kConnecting, kConnected };

    State state_ = State::kSetup;
    char storedSsid_[33] = {0};
    char storedPassword_[65] = {0};
    char setupApSsid_[33] = {0};
    char ipString_[16] = {0};
    uint32_t connectStartedMs_ = 0;
    bool portalStarted_ = false;

    void loadCredentials();
    void saveCredentials(const char* ssid, const char* password);
    void startStaConnect();
    void startSetupPortal();
    void handleRoot();
    void handleSave();
    void handleCaptiveProbe();
#endif
};

extern WifiProvisioning wifiProvisioning;

#endif
