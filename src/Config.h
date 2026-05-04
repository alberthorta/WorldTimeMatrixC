// Persistencia de configuracion en NVS (Preferences).
// Layout:
//   wifi_ssid / wifi_pwd  -> claves separadas (NUNCA exportadas)
//   cfg                    -> JSON serializado con cities + brillo + modo noche + ...
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stdint.h>

namespace Config {

struct WifiCreds {
    String ssid;
    String password;
};

struct City {
    String name;        // <= 6 chars
    float lat;
    float lon;
    uint32_t colorRgb;  // 0xRRGGBB
};

struct NightMode {
    bool enabled;
    uint16_t startMins;     // minutos desde medianoche
    uint16_t endMins;
    float brightness;       // 0.05..1.0
};

struct All {
    float brightness;
    uint16_t weatherRefreshSec;
    bool colonBlink;
    City cities[4];
    NightMode nightMode;
    uint32_t palette[16];     // 0xRRGGBB; slot 0 es transparente
    String rgbOrder;          // "RGB" o "RBG" (G/B intercambiados, segun panel)
};

extern All cfg;             // Instancia global (poblada por begin()).

void begin();               // Carga desde NVS o siembra defaults.
bool save();                // Serializa cfg actual y persiste.

// Helpers para los handlers HTTP.
void writeJson(JsonDocument& doc);    // build JSON respuesta GET /api/config
bool applyPatch(JsonDocument& doc);   // aplicar JSON parcial; true si cities cambian

// WiFi (separado de cfg para no aparecer en backups JSON).
WifiCreds getWifi();
void setWifi(const String& ssid, const String& password);
void clearWifi();

}  // namespace Config
