#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>

#include "RTPMidiConfig.h"

#ifndef ENABLE_OTA
#define ENABLE_OTA 1
#endif

class OtaUpdate {
public:
    void begin(const char* hostname);
    void task(bool wifiReady);
    bool isActive() const { return active_; }

private:
    bool started_ = false;
    bool active_ = false;
    char hostname_[32] = {0};

    void startIfNeeded();
    static void sanitizeHostname(const char* input, char* output, size_t outputSize);
};

extern OtaUpdate otaUpdate;

#endif
