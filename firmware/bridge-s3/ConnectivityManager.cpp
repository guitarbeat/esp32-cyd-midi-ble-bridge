#include "ConnectivityManager.h"
#include "RTPMidiConfig.h"

#if ENABLE_RTP_MIDI
#include "RTPMidiConnection.h"
#include "WifiProvisioning.h"
#endif

#if ENABLE_OTA
#include "OtaUpdate.h"
#endif

ConnectivityManager connectivityManager;

#if ENABLE_RTP_MIDI
static RTPMidiConnection g_rtp;
#endif

ConnectivityManager::ConnectivityManager() {}

void ConnectivityManager::begin()
{
#if ENABLE_RTP_MIDI
    g_rtp.begin("ESP32-MIDI-STATION");
#if ENABLE_OTA
    otaUpdate.begin("esp32-midi-bridge");
#endif
#endif
}

void ConnectivityManager::task()
{
#if ENABLE_RTP_MIDI
    g_rtp.task();
#if ENABLE_OTA
    otaUpdate.task(g_rtp.isWifiConnected());
#endif
#endif
}

bool ConnectivityManager::isConnected() const
{
#if ENABLE_RTP_MIDI
    return g_rtp.hasRtpSession();
#else
    return false;
#endif
}

bool ConnectivityManager::sendMidi(const uint8_t* packet, size_t length)
{
#if ENABLE_RTP_MIDI
    if (g_rtp.hasRtpSession()) {
        g_rtp.sendRawMidi(packet, length);
        return true;
    }
#endif
    return false;
}

bool ConnectivityManager::hasRtpSession() const
{
#if ENABLE_RTP_MIDI
    return g_rtp.hasRtpSession();
#else
    return false;
#endif
}

void ConnectivityManager::startProvisioning()
{
#if ENABLE_RTP_MIDI
    wifiProvisioning.enterSetupMode();
#endif
}
