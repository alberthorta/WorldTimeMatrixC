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

// Snapshot de estado al boot, para diagnostico del flujo de persistencia.
// Histórico: en versiones previas el `cfg` se guardaba como blob en NVS, y eso
// fragmentaba la partición al cabo de muchos saves, llegando a corromper el
// namespace y perder claves (incl. wifi). Ahora el cfg vive en LittleFS y NVS
// retiene sólo wifi creds.
struct DiagInfo {
    uint32_t bootCount;
    // NVS legacy state (para migracion + visibilidad histórica)
    size_t cfgBlobLen;
    size_t cfgStringLen;
    size_t wifiSsidLen;
    size_t wifiPwdLen;
    size_t wxCacheLen;
    size_t nvsUsed;
    size_t nvsFree;
    size_t nvsTotal;
    size_t nvsNamespaces;
    String lastLoad;        // "seeded" | "loaded_fs" | "loaded_nvs_migrated" | "parse_fail:<reason>"
    // Captura del ultimo save(): util para detectar fallos silenciosos.
    uint32_t lastSaveCount;
    size_t lastSaveBytes;
    size_t lastSaveWrote;
    bool lastSaveOk;
    String lastSaveError;
    // FS state
    bool fsMounted;
    size_t fsCfgFileLen;
    size_t fsTotal;
    size_t fsUsed;
};
extern DiagInfo diag;
void captureNvsStatsLive();   // Re-captura nvsUsed/Free/Total desde fuera de begin().
void captureFsStatsLive();    // Re-captura tamaño de /cfg.json y stats LittleFS.

void begin();               // Carga desde NVS o siembra defaults.
bool save();                // Serializa cfg actual y persiste.

// Helpers para los handlers HTTP.
void writeJson(JsonDocument& doc);        // build JSON respuesta GET /api/config (incluye rgb_order para UI)
void writeBackupJson(JsonDocument& doc);  // backup exportable: SOLO lo que esta en cfg.json (sin claves NVS)
bool applyPatch(JsonDocument& doc);       // aplicar JSON parcial; true si cities cambian
bool setRgbOrder(const String& v);        // setter dedicado (NVS); endpoint /api/rgb_order

// WiFi (separado de cfg para no aparecer en backups JSON).
WifiCreds getWifi();
void setWifi(const String& ssid, const String& password);
void clearWifi();

}  // namespace Config
