#include "NetworkServices.h"

#include "RTPMidiConfig.h"

#if ENABLE_RTP_MIDI
#include "RTPMidiConnection.h"
#include "WifiProvisioning.h"
#endif

#if ENABLE_OTA
#include "OtaUpdate.h"
#endif

NetworkServices networkServices;

namespace {

#if ENABLE_RTP_MIDI
RTPMidiConnection g_rtp;
#endif

}  // namespace

bool NetworkServices::begin(const char* sessionName, const char* otaHostname)
{
#if ENABLE_RTP_MIDI
    rtp_ = &g_rtp;
    if (!rtp_->begin(sessionName)) {
        return false;
    }
#if ENABLE_OTA
    otaUpdate.begin(otaHostname);
#endif
    return true;
#else
    (void)sessionName;
    (void)otaHostname;
    return false;
#endif
}

void NetworkServices::task()
{
#if ENABLE_RTP_MIDI
    if (rtp_ != nullptr) {
        rtp_->task();
    }
#if ENABLE_OTA
    otaUpdate.task(isLanReady());
#endif
#endif
}

#if ENABLE_RTP_MIDI
void NetworkServices::enterSetupMode()
{
    wifiProvisioning.enterSetupMode();
}

bool NetworkServices::isLanReady() const
{
    return rtp_ != nullptr && rtp_->isWifiConnected();
}

bool NetworkServices::isSetupMode() const
{
    return rtp_ != nullptr && rtp_->isWifiSetupMode();
}

const char* NetworkServices::setupApSsid() const
{
    return rtp_ != nullptr ? rtp_->wifiSetupApName() : "";
}

const char* NetworkServices::localIpString() const
{
    return rtp_ != nullptr ? rtp_->localIpString() : "0.0.0.0";
}

bool NetworkServices::hasRtpSession() const
{
    return rtp_ != nullptr && rtp_->hasRtpSession();
}

void NetworkServices::forwardMidi(const uint8_t* usbMidiPacket)
{
    if (rtp_ != nullptr) {
        rtp_->sendFromUsbPacket(usbMidiPacket);
    }
}
#endif

#if ENABLE_OTA
bool NetworkServices::isOtaActive() const
{
    return otaUpdate.isActive();
}
#endif
