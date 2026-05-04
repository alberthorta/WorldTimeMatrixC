#include "Config.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>

#include "Icons.h"

namespace Config {

static Preferences prefs;
static const char* NS = "worldtime";

All cfg;

static All defaults() {
    All a;
    a.brightness = 0.5f;
    a.weatherRefreshSec = 60;
    a.colonBlink = true;
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
    doc["rgb_order"] = cfg.rgbOrder;
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
    String rgbo = doc["rgb_order"] | String("");
    if (rgbo == "RGB" || rgbo == "RBG") cfg.rgbOrder = rgbo;
    return citiesChanged;
}

void writeJson(JsonDocument& doc) { buildJson(doc); }
bool applyPatch(JsonDocument& doc) { return applyJson(doc); }

void begin() {
    prefs.begin(NS, /*readOnly=*/false);
    cfg = defaults();

    // Lectura: primero intenta el blob (formato nuevo). Si no existe o esta vacio,
    // hace fallback a getString para migrar configs antiguas; despues re-graba como blob.
    size_t blobLen = prefs.getBytesLength("cfg");
    String json;
    if (blobLen > 0) {
        std::vector<char> buf(blobLen + 1);
        prefs.getBytes("cfg", buf.data(), blobLen);
        buf[blobLen] = 0;
        json = String(buf.data());
    } else {
        json = prefs.getString("cfg", "");
        if (json.length() > 0) {
            Serial.println("[config] migrating cfg from putString -> putBytes");
        }
    }

    if (json.length() == 0) {
        save();
        Serial.println("[config] seeded defaults");
    } else {
        JsonDocument doc;
        if (deserializeJson(doc, json) == DeserializationError::Ok) {
            applyJson(doc);
            Serial.printf("[config] loaded (%u bytes)\n", (unsigned)json.length());
            if (blobLen == 0) save();   // re-graba como blob para futuras lecturas
        } else {
            Serial.println("[config] parse failed, defaults active");
        }
    }
}

bool save() {
    JsonDocument doc;
    buildJson(doc);
    String json;
    serializeJson(doc, json);
    // putBytes (blob) en lugar de putString: el blob puede ser mucho mas grande
    // (varios KB) sin el limite de ~4000 bytes de los strings NVS.
    size_t wrote = prefs.putBytes("cfg", json.c_str(), json.length());
    bool ok = wrote == json.length();
    Serial.printf("[config] saved %u/%u bytes ok=%d\n",
                  (unsigned)wrote, (unsigned)json.length(), ok);
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
