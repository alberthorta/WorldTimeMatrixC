#include "Config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>

#include "Icons.h"

namespace Config {

static Preferences prefs;
static const char* NS = "worldtime";

// Path del fichero donde persistimos la config en LittleFS. Filesystem es
// inmune al ciclo de OTA y aguanta blobs grandes con escrituras frecuentes
// muchisimo mejor que NVS (que se fragmenta y eventualmente corrompe el
// namespace, perdiendo claves silenciosamente).
static const char* CFG_PATH = "/cfg.json";
static const char* CFG_TMP = "/cfg.json.tmp";

All cfg;

DiagInfo diag;

static void captureNvsStats() {
    nvs_stats_t s = {};
    esp_err_t e = nvs_get_stats(NULL, &s);
    if (e == ESP_OK) {
        diag.nvsUsed = s.used_entries;
        diag.nvsFree = s.free_entries;
        diag.nvsTotal = s.total_entries;
        diag.nvsNamespaces = s.namespace_count;
    } else {
        diag.nvsUsed = diag.nvsFree = diag.nvsTotal = diag.nvsNamespaces = 0;
    }
}

static void captureFsStats() {
    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
        diag.fsMounted = false;
        diag.fsTotal = diag.fsUsed = diag.fsCfgFileLen = 0;
        return;
    }
    diag.fsMounted = true;
    diag.fsTotal = LittleFS.totalBytes();
    diag.fsUsed = LittleFS.usedBytes();
    File f = LittleFS.open(CFG_PATH, "r");
    diag.fsCfgFileLen = f ? f.size() : 0;
    if (f) f.close();
}

void captureNvsStatsLive() { captureNvsStats(); }
void captureFsStatsLive() { captureFsStats(); }

// Lee el fichero CFG_PATH y devuelve su contenido. Cadena vacía si no existe
// o si el FS no está montado.
static String readCfgFile() {
    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) return String();
    File f = LittleFS.open(CFG_PATH, "r");
    if (!f) return String();
    String s;
    s.reserve(f.size() + 1);
    while (f.available()) s += (char)f.read();
    f.close();
    return s;
}

// Escribe atómicamente: primero a CFG_TMP, después rename.
static bool writeCfgFile(const String& json) {
    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) return false;
    File f = LittleFS.open(CFG_TMP, "w");
    if (!f) return false;
    size_t wrote = f.print(json);
    f.flush();
    f.close();
    if (wrote != json.length()) {
        LittleFS.remove(CFG_TMP);
        return false;
    }
    // rename atómico (LittleFS sobrescribe si destino existe).
    if (LittleFS.exists(CFG_PATH)) LittleFS.remove(CFG_PATH);
    return LittleFS.rename(CFG_TMP, CFG_PATH);
}

static All defaults() {
    All a;
    a.brightness = 0.5f;
    a.weatherRefreshSec = 60;
    a.colonBlink = true;
    a.hourLeadingZero = true;
    a.omIndicator = false;
    a.secondsIndicator = SecondsIndicator::NONE;
    a.secondsBarColor = 0x333333;
    a.secondsBarWidth = 1;
    a.secondsBarProgress = false;
    a.forecastIndicatorEnabled = false;
    a.forecastIndicatorHorizonH = 1;
    a.forecastThresh1 = 0.5f;
    a.forecastThresh2 = 1.5f;
    a.forecastThresh3 = 3.0f;
    a.forecastColorRising  = 0x00C000;
    a.forecastColorFalling = 0xC00000;
    a.forecastColorStable  = 0x666666;
    a.cities[0] = {"BCN",   41.41651f,    2.177195f, 0xCCCCCC};
    a.cities[1] = {"NEGRA", -32.88946f,  -68.8458f,  0xCCCCCC};
    a.cities[2] = {"MAMI",   10.64232f,  -71.61088f, 0xCCCCCC};
    a.cities[3] = {"YAKI",    4.60971f,  -74.08174f, 0xCCCCCC};
    a.nightMode = {false, (uint16_t)(22 * 60), (uint16_t)(7 * 60), 0.1f};
    memcpy(a.palette, Icons::DEFAULT_PALETTE, sizeof(a.palette));
    a.rgbOrder = "RGB";       // estandar; cambiar a "RBG" si el panel cablea G/B intercambiados
    return a;
}

static void buildJson(JsonDocument& doc) {
    doc["brightness"] = cfg.brightness;
    doc["weather_refresh_sec"] = cfg.weatherRefreshSec;
    doc["colon_blink"] = cfg.colonBlink;
    doc["hour_leading_zero"] = cfg.hourLeadingZero;
    doc["om_indicator"] = cfg.omIndicator;
    doc["seconds_indicator"] = (cfg.secondsIndicator == SecondsIndicator::MARKER) ? "marker"
                              : (cfg.secondsIndicator == SecondsIndicator::BAR)   ? "bar"
                                                                                   : "none";
    doc["seconds_bar_color"] = cfg.secondsBarColor;
    doc["seconds_bar_width"] = cfg.secondsBarWidth;
    doc["seconds_bar_progress"] = cfg.secondsBarProgress;
    doc["forecast_indicator_enabled"] = cfg.forecastIndicatorEnabled;
    doc["forecast_indicator_horizon_h"] = cfg.forecastIndicatorHorizonH;
    doc["forecast_thresh_1"] = cfg.forecastThresh1;
    doc["forecast_thresh_2"] = cfg.forecastThresh2;
    doc["forecast_thresh_3"] = cfg.forecastThresh3;
    doc["forecast_color_rising"]  = cfg.forecastColorRising;
    doc["forecast_color_falling"] = cfg.forecastColorFalling;
    doc["forecast_color_stable"]  = cfg.forecastColorStable;
    JsonArray cities = doc["cities"].to<JsonArray>();
    for (int i = 0; i < 4; i++) {
        const City& c = cfg.cities[i];
        JsonObject o = cities.add<JsonObject>();
        o["name"] = c.name;
        o["lat"] = c.lat;
        o["lon"] = c.lon;
        o["color"] = c.colorRgb;
    }
    JsonObject nm = doc["night_mode"].to<JsonObject>();
    nm["enabled"] = cfg.nightMode.enabled;
    nm["start_mins"] = cfg.nightMode.startMins;
    nm["end_mins"] = cfg.nightMode.endMins;
    nm["brightness"] = cfg.nightMode.brightness;
    JsonArray pal = doc["palette"].to<JsonArray>();
    for (int i = 0; i < 16; i++) pal.add(cfg.palette[i]);
    // rgb_order NO se incluye aqui: vive en NVS por ser identidad de hardware
    // (cableado G/B del panel especifico) y no debe viajar en backups
    // exportables. writeJson() lo añade aparte para responder /api/config.
    Icons::serializeAll(doc["icons"].to<JsonObject>());
}

// Aplica los campos presentes en `doc` a cfg y a Icons::icons[].
// Devuelve true si lat/lon/name de alguna ciudad cambio (necesita refetch).
static bool applyJson(JsonDocument& doc) {
    bool citiesChanged = false;
    if (doc["brightness"].is<float>()) {
        float b = doc["brightness"];
        cfg.brightness = constrain(b, 0.05f, 1.0f);
    }
    if (doc["weather_refresh_sec"].is<int>()) {
        cfg.weatherRefreshSec = max(30, (int)doc["weather_refresh_sec"]);
    }
    if (doc["colon_blink"].is<bool>()) {
        cfg.colonBlink = doc["colon_blink"];
    }
    if (doc["hour_leading_zero"].is<bool>()) {
        cfg.hourLeadingZero = doc["hour_leading_zero"];
    }
    if (doc["om_indicator"].is<bool>()) {
        cfg.omIndicator = doc["om_indicator"];
    }
    // Indicador de segundos: nuevo formato (string + color) con migracion del
    // legacy `seconds_bar: bool` (true → marker, false → none) si solo viene
    // ese campo. Si vienen ambos, gana el nuevo.
    if (doc["seconds_indicator"].is<const char*>()) {
        String s = doc["seconds_indicator"].as<const char*>();
        if (s == "marker")     cfg.secondsIndicator = SecondsIndicator::MARKER;
        else if (s == "bar")   cfg.secondsIndicator = SecondsIndicator::BAR;
        else                   cfg.secondsIndicator = SecondsIndicator::NONE;
    } else if (doc["seconds_bar"].is<bool>()) {
        cfg.secondsIndicator = doc["seconds_bar"]
            ? SecondsIndicator::MARKER
            : SecondsIndicator::NONE;
    }
    if (doc["seconds_bar_color"].is<unsigned int>() ||
        doc["seconds_bar_color"].is<int>()) {
        cfg.secondsBarColor = (uint32_t)(doc["seconds_bar_color"].as<unsigned int>()) & 0xFFFFFFu;
    }
    if (doc["seconds_bar_width"].is<int>()) {
        int w = doc["seconds_bar_width"];
        if (w < 1)  w = 1;
        if (w > 16) w = 16;
        cfg.secondsBarWidth = (uint8_t)w;
    }
    if (doc["seconds_bar_progress"].is<bool>()) {
        cfg.secondsBarProgress = doc["seconds_bar_progress"];
    }
    if (doc["forecast_indicator_enabled"].is<bool>()) {
        cfg.forecastIndicatorEnabled = doc["forecast_indicator_enabled"];
    }
    if (doc["forecast_indicator_horizon_h"].is<int>()) {
        int h = doc["forecast_indicator_horizon_h"];
        cfg.forecastIndicatorHorizonH = (h == 2) ? 2 : 1;
    }
    auto applyThresh = [&](const char* key, float& dst) {
        if (doc[key].is<float>() || doc[key].is<int>()) {
            float v = doc[key].as<float>();
            if (v < 0.0f)  v = 0.0f;
            if (v > 50.0f) v = 50.0f;
            dst = v;
        }
    };
    applyThresh("forecast_thresh_1", cfg.forecastThresh1);
    applyThresh("forecast_thresh_2", cfg.forecastThresh2);
    applyThresh("forecast_thresh_3", cfg.forecastThresh3);
    auto applyColor = [&](const char* key, uint32_t& dst) {
        if (doc[key].is<unsigned int>() || doc[key].is<int>()) {
            dst = (uint32_t)(doc[key].as<unsigned int>()) & 0xFFFFFFu;
        }
    };
    applyColor("forecast_color_rising",  cfg.forecastColorRising);
    applyColor("forecast_color_falling", cfg.forecastColorFalling);
    applyColor("forecast_color_stable",  cfg.forecastColorStable);
    JsonArray arr = doc["cities"].as<JsonArray>();
    if (!arr.isNull()) {
        for (int i = 0; i < 4 && i < (int)arr.size(); i++) {
            City& c = cfg.cities[i];
            String oldName = c.name;
            float oldLat = c.lat, oldLon = c.lon;
            c.name = arr[i]["name"] | c.name;
            c.lat = arr[i]["lat"] | c.lat;
            c.lon = arr[i]["lon"] | c.lon;
            c.colorRgb = arr[i]["color"] | c.colorRgb;
            if (oldName != c.name || oldLat != c.lat || oldLon != c.lon) {
                citiesChanged = true;
            }
        }
    }
    JsonObject nm = doc["night_mode"].as<JsonObject>();
    if (!nm.isNull()) {
        if (nm["enabled"].is<bool>()) cfg.nightMode.enabled = nm["enabled"];
        if (nm["start_mins"].is<int>()) cfg.nightMode.startMins = nm["start_mins"];
        if (nm["end_mins"].is<int>()) cfg.nightMode.endMins = nm["end_mins"];
        if (nm["brightness"].is<float>()) {
            float b = nm["brightness"];
            cfg.nightMode.brightness = constrain(b, 0.05f, 1.0f);
        }
    }
    JsonArray pal = doc["palette"].as<JsonArray>();
    if (!pal.isNull()) {
        for (int i = 0; i < 16 && i < (int)pal.size(); i++) {
            cfg.palette[i] = pal[i] | cfg.palette[i];
        }
    }
    JsonObjectConst icons = doc["icons"].as<JsonObjectConst>();
    if (!icons.isNull()) Icons::deserializeAll(icons);
    // rgb_order NO se aplica desde aqui — es identidad per-device almacenada
    // en NVS. Se setea via setRgbOrder() (endpoint dedicado /api/rgb_order),
    // asi un restore desde backup no la sobreescribe accidentalmente.
    return citiesChanged;
}

// writeJson para /api/config: incluye rgb_order para que la UI lo muestre,
// aunque no este en cfg.json (no viaja en backups).
void writeJson(JsonDocument& doc) {
    buildJson(doc);
    doc["rgb_order"] = cfg.rgbOrder;
}

// writeBackupJson: solo lo que vive en cfg.json (sin rgb_order ni claves NVS).
// Lo que devuelve este builder es exactamente el contenido del fichero
// persistido y lo que un restore aceptaria de vuelta.
void writeBackupJson(JsonDocument& doc) { buildJson(doc); }

// Setter dedicado para rgb_order (endpoint /api/rgb_order).
bool setRgbOrder(const String& v) {
    if (v != "RGB" && v != "RBG") return false;
    if (cfg.rgbOrder != v) {
        cfg.rgbOrder = v;
        prefs.putString("rgb_order", v);
    }
    return true;
}

// --- Premium providers (NVS-only, per-device, no en backups) ---
// Tomorrow.io: claves NVS wx_tio_en (bool), wx_tio_key (string), wx_tio_ref (uint16).
// WeatherAPI:  claves NVS wx_wap_en (bool), wx_wap_key (string), wx_wap_ref (uint16).
// Exclusion mutua: si setX(enabled=true) se llama, el OTRO se pone a enabled=false.
// Las api_keys y refresh se mantienen guardadas en cualquier caso para que el
// usuario pueda alternar sin reescribirlas.
TomorrowSettings getTomorrowSettings() {
    TomorrowSettings s;
    s.enabled = prefs.getBool("wx_tio_en", false);
    s.apiKey = prefs.getString("wx_tio_key", "");
    s.refreshSec = prefs.getUShort("wx_tio_ref", 14400);   // 4h por defecto (free tier 25 calls/dia)
    return s;
}

void setTomorrowSettings(bool enabled, const String& apiKey, uint16_t refreshSec) {
    prefs.putBool("wx_tio_en", enabled);
    prefs.putString("wx_tio_key", apiKey);
    if (refreshSec < 60) refreshSec = 60;
    prefs.putUShort("wx_tio_ref", refreshSec);
    if (enabled) prefs.putBool("wx_wap_en", false);   // exclusion mutua
}

bool hasTomorrowSettings() {
    TomorrowSettings s = getTomorrowSettings();
    return s.enabled && s.apiKey.length() > 0;
}

WeatherApiSettings getWeatherApiSettings() {
    WeatherApiSettings s;
    s.enabled = prefs.getBool("wx_wap_en", false);
    s.apiKey = prefs.getString("wx_wap_key", "");
    s.refreshSec = prefs.getUShort("wx_wap_ref", 1800);    // 30min por defecto (free tier 1M/mes, holgadisimo)
    return s;
}

void setWeatherApiSettings(bool enabled, const String& apiKey, uint16_t refreshSec) {
    prefs.putBool("wx_wap_en", enabled);
    prefs.putString("wx_wap_key", apiKey);
    if (refreshSec < 60) refreshSec = 60;
    prefs.putUShort("wx_wap_ref", refreshSec);
    if (enabled) prefs.putBool("wx_tio_en", false);   // exclusion mutua
}

bool hasWeatherApiSettings() {
    WeatherApiSettings s = getWeatherApiSettings();
    return s.enabled && s.apiKey.length() > 0;
}

PremiumProvider activePremium() {
    // Si por algun bug ambos quedaran enabled=true (pre-mutex), gana TIO.
    if (hasTomorrowSettings()) return PremiumProvider::TOMORROW;
    if (hasWeatherApiSettings()) return PremiumProvider::WEATHERAPI;
    return PremiumProvider::NONE;
}
bool applyPatch(JsonDocument& doc) { return applyJson(doc); }

void begin() {
    captureNvsStats();
    Serial.printf("[nvs] used=%u free=%u total=%u namespaces=%u\n",
                  (unsigned)diag.nvsUsed, (unsigned)diag.nvsFree,
                  (unsigned)diag.nvsTotal, (unsigned)diag.nvsNamespaces);

    prefs.begin(NS, /*readOnly=*/false);
    cfg = defaults();

    // Boot counter (instrumentación añadida durante el debug del bug OTA).
    diag.bootCount = prefs.getUInt("boot_n", 0) + 1;
    prefs.putUInt("boot_n", diag.bootCount);
    Serial.printf("[nvs] boot_count=%u\n", (unsigned)diag.bootCount);

    // Snapshot de claves NVS legacy (para migración desde versiones anteriores).
    diag.cfgBlobLen = prefs.getBytesLength("cfg");
    diag.cfgStringLen = prefs.getString("cfg", "").length();
    diag.wifiSsidLen = prefs.getString("wifi_ssid", "").length();
    diag.wifiPwdLen = prefs.getString("wifi_pwd", "").length();
    diag.wxCacheLen = prefs.getBytesLength("wxcache");
    Serial.printf("[nvs] cfg blob=%u str=%u | wifi ssid=%u pwd=%u | wxcache=%u\n",
                  (unsigned)diag.cfgBlobLen, (unsigned)diag.cfgStringLen,
                  (unsigned)diag.wifiSsidLen, (unsigned)diag.wifiPwdLen,
                  (unsigned)diag.wxCacheLen);

    // 1) Intenta cargar desde LittleFS (path nuevo, robusto contra OTA).
    String json = readCfgFile();
    bool fromFs = json.length() > 0;

    // 2) Si no hay fichero, intenta migrar desde NVS legacy (blob o string).
    if (!fromFs && diag.cfgBlobLen > 0) {
        std::vector<char> buf(diag.cfgBlobLen + 1);
        prefs.getBytes("cfg", buf.data(), diag.cfgBlobLen);
        buf[diag.cfgBlobLen] = 0;
        json = String(buf.data());
        Serial.println("[config] migrating from NVS blob -> LittleFS");
    } else if (!fromFs) {
        String legacy = prefs.getString("cfg", "");
        if (legacy.length() > 0) {
            json = legacy;
            Serial.println("[config] migrating from NVS string -> LittleFS");
        }
    }

    if (json.length() > 0) {
        Serial.printf("[config] cfg head: %.80s\n", json.c_str());
    }

    if (json.length() == 0) {
        diag.lastLoad = "seeded";
        save();
        Serial.println("[config] seeded defaults to LittleFS");
    } else {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, json);
        if (err == DeserializationError::Ok) {
            applyJson(doc);
            if (fromFs) {
                diag.lastLoad = "loaded_fs";
                Serial.printf("[config] loaded from LittleFS (%u bytes)\n",
                              (unsigned)json.length());
            } else {
                diag.lastLoad = "loaded_nvs_migrated";
                Serial.printf("[config] migrated from NVS (%u bytes), saving to FS\n",
                              (unsigned)json.length());
                save();   // graba a LittleFS
                // Limpia NVS legacy para liberar entries fragmentadas.
                prefs.remove("cfg");
                prefs.remove("wxcache");  // tambien movida a FS
                Serial.println("[config] cleared legacy NVS cfg + wxcache keys");
            }
        } else {
            diag.lastLoad = String("parse_fail:") + err.c_str();
            Serial.printf("[config] parse failed (%s), defaults active\n", err.c_str());
        }
    }

    // Limpieza one-shot de wxcache legacy en NVS (puede haber sobrevivido si el
    // primer boot tras el cambio a FS se hizo via /api/firmware sin pasar por
    // la rama "loaded_nvs_migrated"). Idempotente: no hace nada si no existe.
    if (prefs.getBytesLength("wxcache") > 0 || prefs.getString("wxcache", "").length() > 0) {
        prefs.remove("wxcache");
        Serial.println("[nvs] removed legacy wxcache key");
    }

    // rgb_order: NVS es la fuente de verdad. Si tiene valor, sobrescribe lo que
    // applyJson haya cargado de cfg.json. En el primer boot tras el cambio,
    // applyJson ya escribio NVS desde el cfg.json legacy, asi que ambos coinciden.
    String storedRgb = prefs.getString("rgb_order", "");
    if (storedRgb == "RGB" || storedRgb == "RBG") {
        cfg.rgbOrder = storedRgb;
    }
    Serial.printf("[config] rgb_order=%s (source=%s)\n",
                  cfg.rgbOrder.c_str(),
                  storedRgb.length() > 0 ? "nvs" : "default/cfg.json");

    captureNvsStats();
    captureFsStats();
    Serial.printf("[nvs] after-load used=%u free=%u | [fs] mounted=%d cfg=%u total=%u used=%u\n",
                  (unsigned)diag.nvsUsed, (unsigned)diag.nvsFree,
                  diag.fsMounted, (unsigned)diag.fsCfgFileLen,
                  (unsigned)diag.fsTotal, (unsigned)diag.fsUsed);
}

bool save() {
    JsonDocument doc;
    buildJson(doc);
    String json;
    serializeJson(doc, json);

    bool ok = writeCfgFile(json);
    diag.lastSaveCount++;
    diag.lastSaveBytes = json.length();
    diag.lastSaveWrote = ok ? json.length() : 0;
    diag.lastSaveOk = ok;
    diag.lastSaveError = ok ? String("") : String("fs_write_fail");
    captureFsStats();
    Serial.printf("[config] saved %u bytes ok=%d fs(used=%u/%u cfgFile=%u)\n",
                  (unsigned)json.length(), ok,
                  (unsigned)diag.fsUsed, (unsigned)diag.fsTotal,
                  (unsigned)diag.fsCfgFileLen);
    return ok;
}

WifiCreds getWifi() {
    WifiCreds w;
    w.ssid = prefs.getString("wifi_ssid", "");
    w.password = prefs.getString("wifi_pwd", "");
    return w;
}

void setWifi(const String& ssid, const String& password) {
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pwd", password);
}

void clearWifi() {
    prefs.remove("wifi_ssid");
    prefs.remove("wifi_pwd");
}

}  // namespace Config
