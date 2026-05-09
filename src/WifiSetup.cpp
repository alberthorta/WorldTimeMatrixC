#include "WifiSetup.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>

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
    // Hostname ANTES de begin: si lo cambiamos despues, DHCP ya negocio con
    // el nombre auto-generado y algunos APs/routers tardan en propagar el
    // nuevo, dejando el device alcanzable solo via IP por minutos.
    // Pre-fix con last3 del MAC para que sea unico hasta que sepamos el IP.
    uint8_t mac[6]; WiFi.macAddress(mac);
    char tmpHost[24];
    snprintf(tmpHost, sizeof(tmpHost), "WorldTime-%02X%02X%02X", mac[3], mac[4], mac[5]);
    WiFi.setHostname(tmpHost);
    // Power save off: por defecto el ESP32 entra en sleep entre beacons.
    // En APs marginales esto retrasa muchisimo la atencion de TCP incoming
    // y puede hacer que el server HTTP "tarde" en responder aunque este up.
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
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
    Serial.printf("[wifi] ok ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    // Forzar 802.11b post-asociacion: DSSS modulation aguanta bajo RSSI mucho
    // mejor que OFDM (11g/n). Trade-off: max 11 Mbps en lugar de 54+ — para
    // JSON pequeño es más que suficiente y el link queda estable, sin la fase
    // de minutos de rate adaptation que sufrimos en redes marginales.
    esp_err_t e = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B);
    Serial.printf("[wifi] force 11b: %s\n", e == ESP_OK ? "ok" : "fail");
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

void switchToAp() {
    if (g_mode == Mode::Ap) return;
    Serial.println("[wifi] switching to AP mode (runtime)");
    WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
    delay(100);
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(AP_SSID, AP_PASSWORD);
    if (ok) {
        Serial.printf("[wifi] AP up: ssid=%s ip=%s\n",
                      AP_SSID, WiFi.softAPIP().toString().c_str());
        g_mode = Mode::Ap;
        g_ssid = "";
        g_hostname = "";
    } else {
        Serial.println("[wifi] AP softAP failed");
    }
}

// Health-check periodico:
//   1) Beacon UDP broadcast cada 60s para refrescar el bridging del AP y
//      evitar que dropee al cliente por inactividad inbound (aging-out).
//   2) Probe TCP al gateway:53 cada 2 min. Si falla 2 veces seguidas,
//      forzar reconnect — la cola lwIP probablemente este enterrada por
//      retransmits acumulados en RSSI marginal y no se recupera sola.
//   3) Failsafe duro: reconnect cada 30 min sin importar el estado. Coste
//      5-10s de outage; beneficio: nunca queda colgado mas alla de eso.
void tickHealth() {
    if (g_mode != Mode::Sta) return;
    static uint32_t lastBeaconMs = 0;
    static uint32_t lastProbeMs = 0;
    static uint32_t lastReconnectMs = 0;
    static int consecutiveFails = 0;
    uint32_t now = millis();
    if (lastReconnectMs == 0) lastReconnectMs = now;

    // (3) Failsafe duro 30 min.
    if ((now - lastReconnectMs) >= 1800000UL) {
        Serial.println("[wifi] periodic reconnect (failsafe 30min)");
        WiFi.reconnect();
        lastReconnectMs = now;
        lastBeaconMs = now;
        lastProbeMs = now;
        consecutiveFails = 0;
        return;
    }

    // (1) Beacon UDP broadcast cada 60s.
    if ((now - lastBeaconMs) >= 60000UL && WiFi.status() == WL_CONNECTED) {
        lastBeaconMs = now;
        WiFiUDP udp;
        if (udp.beginPacket(IPAddress(255, 255, 255, 255), 5353)) {
            const uint8_t kPing[2] = {'W', 'T'};
            udp.write(kPing, sizeof(kPing));
            udp.endPacket();
        }
    }

    // (2) Probe TCP al gateway:53 cada 2 min. Detecta el "stuck" donde el
    // device esta asociado pero la cola lwIP no avanza. setTimeout corto
    // (1.5s) para no bloquear el loop. Si falla 2 veces seguidas, reconnect.
    const uint32_t PROBE_INTERVAL_MS = 120000UL;
    const int PROBE_FAIL_THRESHOLD = 2;
    if (WiFi.status() == WL_CONNECTED && (now - lastProbeMs) >= PROBE_INTERVAL_MS) {
        lastProbeMs = now;
        IPAddress gw = WiFi.gatewayIP();
        if (gw == IPAddress(0, 0, 0, 0)) return;
        WiFiClient c;
        c.setTimeout(1500);
        bool ok = c.connect(gw, 53);
        if (c.connected()) c.stop();
        if (ok) {
            consecutiveFails = 0;
        } else {
            consecutiveFails++;
            Serial.printf("[wifi] probe fail %d/%d gw=%s\n",
                          consecutiveFails, PROBE_FAIL_THRESHOLD,
                          gw.toString().c_str());
            if (consecutiveFails >= PROBE_FAIL_THRESHOLD) {
                Serial.println("[wifi] forcing reconnect (gateway unreachable)");
                WiFi.reconnect();
                consecutiveFails = 0;
                lastReconnectMs = now;
                lastBeaconMs = now;
                lastProbeMs = now + 30000UL;   // skip probe 30s tras reconnect
            }
        }
    }
}

}  // namespace WifiSetup
