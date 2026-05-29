#include "WifiProvisioning.h"

#include "RTPMidiConfig.h"

#if ENABLE_RTP_MIDI

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

WifiProvisioning wifiProvisioning;

namespace {

constexpr char kNamespace[] = "piano_wifi";
constexpr char kKeySsid[] = "ssid";
constexpr char kKeyPassword[] = "pass";
constexpr uint32_t kConnectTimeoutMs = 25000;
constexpr uint16_t kDnsPort = 53;

DNSServer dnsServer;
WebServer webServer(80);

String buildSetupPage()
{
    String html = R"(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Piano BLE Bridge WiFi</title>
<style>
body{font-family:system-ui,sans-serif;margin:24px;background:#0b1220;color:#e8eef8}
h1{font-size:1.25rem;margin:0 0 8px}
p{color:#9fb0cc;font-size:.9rem;line-height:1.4}
label{display:block;margin-top:16px;font-size:.85rem}
input,select{width:100%;box-sizing:border-box;margin-top:6px;padding:10px;border-radius:8px;border:1px solid #2a3a55;background:#111a2b;color:#fff}
button{margin-top:20px;width:100%;padding:12px;border:0;border-radius:8px;background:#2ec4b6;color:#042018;font-weight:600;font-size:1rem}
</style></head><body>
<h1>Piano BLE Bridge</h1>
<p>Connect this device to your home WiFi for Apple MIDI (RTP-MIDI). Bluetooth MIDI keeps working.</p>
<form method="POST" action="/save">
<label for="ssid">WiFi network</label>
<input id="ssid" name="ssid" list="networks" required placeholder="Network name">
<datalist id="networks">
)";

    const int count = WiFi.scanNetworks();
    for (int i = 0; i < count; i++) {
        const String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) {
            continue;
        }
        html += "<option value=\"";
        html += ssid;
        html += "\"></option>";
    }

    html += R"(</datalist>
<label for="pass">Password</label>
<input id="pass" name="pass" type="password" autocomplete="off">
<button type="submit">Save and connect</button>
</form>
<p>After saving, the board reboots and joins your network. On Mac: Audio MIDI Setup → Network → add this IP on port 5004.</p>
</body></html>)";

    return html;
}

}  // namespace

void WifiProvisioning::loadCredentials()
{
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) {
        return;
    }

    const String ssid = prefs.getString(kKeySsid, "");
    const String password = prefs.getString(kKeyPassword, "");
    prefs.end();

    strncpy(storedSsid_, ssid.c_str(), sizeof(storedSsid_) - 1);
    storedSsid_[sizeof(storedSsid_) - 1] = '\0';
    strncpy(storedPassword_, password.c_str(), sizeof(storedPassword_) - 1);
    storedPassword_[sizeof(storedPassword_) - 1] = '\0';

#if defined(WIFI_SSID_TEXT) && defined(WIFI_PASSWORD_TEXT)
    if (storedSsid_[0] == '\0' && WIFI_SSID_TEXT[0] != '\0') {
        strncpy(storedSsid_, WIFI_SSID_TEXT, sizeof(storedSsid_) - 1);
        storedSsid_[sizeof(storedSsid_) - 1] = '\0';
        strncpy(storedPassword_, WIFI_PASSWORD_TEXT, sizeof(storedPassword_) - 1);
        storedPassword_[sizeof(storedPassword_) - 1] = '\0';
    }
#endif
}

void WifiProvisioning::saveCredentials(const char* ssid, const char* password)
{
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return;
    }
    prefs.putString(kKeySsid, ssid);
    prefs.putString(kKeyPassword, password != nullptr ? password : "");
    prefs.end();
}

void WifiProvisioning::startSetupPortal()
{
    state_ = State::kSetup;
    ipString_[0] = '\0';
    connectStartedMs_ = 0;

    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP_STA);
    WiFi.scanNetworks(true, true);
    WiFi.softAP(setupApSsid_, nullptr, 1, 0, 4);
    dnsServer.start(kDnsPort, "*", WiFi.softAPIP());

    if (!portalStarted_) {
        webServer.on("/", [this]() { handleRoot(); });
        webServer.on("/save", HTTP_POST, [this]() { handleSave(); });
        webServer.on("/generate_204", [this]() { handleCaptiveProbe(); });
        webServer.on("/hotspot-detect.html", [this]() { handleCaptiveProbe(); });
        webServer.on("/fwlink", [this]() { handleCaptiveProbe(); });
        webServer.onNotFound([this]() { handleCaptiveProbe(); });
        webServer.begin();
        portalStarted_ = true;
    }

    strncpy(ipString_, WiFi.softAPIP().toString().c_str(), sizeof(ipString_) - 1);
    ipString_[sizeof(ipString_) - 1] = '\0';
    Serial.printf("[WiFi] Setup AP \"%s\" at %s — join and open http://%s\n",
                  setupApSsid_,
                  ipString_,
                  ipString_);
}

void WifiProvisioning::startStaConnect()
{
    state_ = State::kConnecting;
    connectStartedMs_ = millis();
    ipString_[0] = '\0';

    WiFi.mode(WIFI_STA);
    WiFi.begin(storedSsid_, storedPassword_);
    Serial.printf("[WiFi] Connecting to \"%s\"...\n", storedSsid_);
}

void WifiProvisioning::handleRoot()
{
    webServer.send(200, "text/html", buildSetupPage());
}

void WifiProvisioning::handleSave()
{
    const String ssid = webServer.arg("ssid");
    const String password = webServer.arg("pass");

    if (ssid.length() == 0) {
        webServer.send(400, "text/plain", "SSID required");
        return;
    }

    saveCredentials(ssid.c_str(), password.c_str());
    webServer.send(200, "text/html",
                    "<html><body style='font-family:sans-serif;background:#0b1220;color:#e8eef8;padding:24px'>"
                    "<h1>Saved</h1><p>Rebooting to join your WiFi...</p></body></html>");
    delay(500);
    ESP.restart();
}

void WifiProvisioning::handleCaptiveProbe()
{
    webServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
    webServer.send(302, "text/plain", "");
}

bool WifiProvisioning::begin(const char* setupApBaseName)
{
    portalStarted_ = false;

    const char* base = (setupApBaseName != nullptr && setupApBaseName[0] != '\0') ? setupApBaseName
                                                                                  : "Piano BLE Bridge";
    snprintf(setupApSsid_, sizeof(setupApSsid_), "%.24s-Setup", base);
    for (size_t i = 0; setupApSsid_[i] != '\0'; i++) {
        if (setupApSsid_[i] == ' ') {
            setupApSsid_[i] = '-';
        }
    }

    loadCredentials();
    if (storedSsid_[0] == '\0') {
        startSetupPortal();
        return true;
    }

    startStaConnect();
    return true;
}

void WifiProvisioning::enterSetupMode()
{
    Serial.println("[WiFi] Entering setup mode");
    startSetupPortal();
}

void WifiProvisioning::task()
{
    if (state_ == State::kSetup) {
        dnsServer.processNextRequest();
        webServer.handleClient();
        return;
    }

    if (state_ == State::kConnecting) {
        if (WiFi.status() == WL_CONNECTED) {
            state_ = State::kConnected;
            strncpy(ipString_, WiFi.localIP().toString().c_str(), sizeof(ipString_) - 1);
            ipString_[sizeof(ipString_) - 1] = '\0';
            Serial.printf("[WiFi] Connected %s\n", ipString_);
            return;
        }

        if (millis() - connectStartedMs_ >= kConnectTimeoutMs) {
            Serial.printf("[WiFi] Could not join \"%s\" — opening setup AP\n", storedSsid_);
            startSetupPortal();
        }
        return;
    }
}

bool WifiProvisioning::isConnected() const
{
    return state_ == State::kConnected;
}

bool WifiProvisioning::isSetupMode() const
{
    return state_ == State::kSetup;
}

const char* WifiProvisioning::localIpString() const
{
    return ipString_;
}

const char* WifiProvisioning::setupApSsid() const
{
    return setupApSsid_;
}

#else

WifiProvisioning wifiProvisioning;

bool WifiProvisioning::begin(const char* setupApBaseName)
{
    (void)setupApBaseName;
    return false;
}

void WifiProvisioning::task() {}

void WifiProvisioning::enterSetupMode() {}

bool WifiProvisioning::isConnected() const
{
    return false;
}

bool WifiProvisioning::isSetupMode() const
{
    return false;
}

const char* WifiProvisioning::localIpString() const
{
    return "";
}

const char* WifiProvisioning::setupApSsid() const
{
    return "";
}

#endif
