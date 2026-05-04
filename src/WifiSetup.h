// Conecta a WiFi en modo STA usando creds en NVS. Si fallan o no hay,
// levanta un AP (WorldTime-Setup / matrixportal) para reconfigurar via web.
#pragma once

#include <Arduino.h>
#include <vector>

namespace WifiSetup {

enum class Mode { None, Sta, Ap };

struct ScanResult {
    String ssid;
    int32_t rssi;
    bool secure;
};

struct ScanReply {
    int rawCount;                      // valor crudo de WiFi.scanNetworks (puede ser <0)
    int modeBefore;                    // wifi_mode_t antes del scan
    std::vector<ScanResult> nets;
};

extern const char* AP_SSID;
extern const char* AP_PASSWORD;

Mode begin();                          // Llamar una vez en setup().
Mode currentMode();
String currentSsid();                  // SSID activo (vacio en AP/None).
String currentIp();                    // IP de la interfaz activa.
String currentHostname();              // "WorldTimeXXX" en STA, vacio en AP/None.
ScanReply scan();                      // Escanea redes cercanas (con diagnostico).

}  // namespace WifiSetup
