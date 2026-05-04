#include "WifiSetup.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "Config.h"

namespace WifiSetup {

const char* AP_SSID = "WorldTime-Setup";
const char* AP_PASSWORD = "matrixportal";  // >= 8 chars

static Mode g_mode = Mode::None;
static String g_ssid;
static String g_hostname;

static bool tryConnect(const String& ssid, const String& password, uint32_t timeoutMs) {
    if (ssid.isEmpty()) return false;
    Serial.printf("[wifi] connect to %s\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) {
            Serial.println("[wifi] timeout");
            WiFi.disconnect();
            return false;
        }
        delay(200);
    }
    Serial.printf("[wifi] ok ip=%s\n", WiFi.localIP().toString().c_str());
    return true;
}

Mode begin() {
    Config::WifiCreds w = Config::getWifi();
    if (tryConnect(w.ssid, w.password, /*timeoutMs=*/15000)) {
        g_mode = Mode::Sta;
        g_ssid = w.ssid;
        // Hostname WorldTimeXXX (ultimo octeto de la IP) + mDNS para que aparezca
        // en routers, dispositivos compatibles con Bonjour, etc.
        IPAddress ip = WiFi.localIP();
        char host[24];
        snprintf(host, sizeof(host), "WorldTime%d", (int)ip[3]);
        WiFi.setHostname(host);
        g_hostname = host;
        if (MDNS.begin(host)) {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("[mdns] up: %s.local\n", host);
        } else {
            Serial.println("[mdns] begin failed");
        }
        return g_mode;
    }
    Serial.println("[wifi] starting AP for setup");
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(AP_SSID, AP_PASSWORD);
    if (ok) {
        Serial.printf("[wifi] AP up: ssid=%s pwd=%s ip=%s\n",
                      AP_SSID, AP_PASSWORD, WiFi.softAPIP().toString().c_str());
        g_mode = Mode::Ap;
    } else {
        Serial.println("[wifi] AP failed");
        g_mode = Mode::None;
    }
    return g_mode;
}

Mode currentMode() { return g_mode; }
String currentSsid() { return g_ssid; }
String currentHostname() { return g_hostname; }

String currentIp() {
    if (g_mode == Mode::Sta) return WiFi.localIP().toString();
    if (g_mode == Mode::Ap) return WiFi.softAPIP().toString();
    return "";
}

ScanReply scan() {
    ScanReply rep;
    wifi_mode_t prev = WiFi.getMode();
    rep.modeBefore = (int)prev;
    if (prev == WIFI_AP) {
        WiFi.mode(WIFI_AP_STA);
        delay(100);
    }
    int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false,
                              /*passive=*/false, /*max_ms_per_chan=*/300);
    rep.rawCount = n;
    if (n > 0) {
        rep.nets.reserve(n);
        for (int i = 0; i < n; i++) {
            ScanResult r;
            r.ssid = WiFi.SSID(i);
            r.rssi = WiFi.RSSI(i);
            r.secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            if (!r.ssid.isEmpty()) rep.nets.push_back(r);
        }
    }
    WiFi.scanDelete();
    if (prev == WIFI_AP) {
        WiFi.mode(WIFI_AP);
    }
    return rep;
}

}  // namespace WifiSetup
