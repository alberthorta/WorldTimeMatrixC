#include "Weather.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <math.h>

#include "Config.h"

namespace Weather {

Data data[4] = {};
FetchDebug debugInfo = {-1, 0, ""};
static TaskHandle_t taskHandle = nullptr;

static bool fetchOne(int idx) {
    debugInfo.attempts++;
    if (!WiFi.isConnected()) {
        debugInfo.lastError = "no wifi";
        debugInfo.httpCode = -1;
        return false;
    }
    const Config::City& cc = Config::cfg.cities[idx];
    Data& d = data[idx];

    String url = "http://api.open-meteo.com/v1/forecast?latitude=";
    url += String(cc.lat, 6);
    url += "&longitude=";
    url += String(cc.lon, 6);
    url += "&current=temperature_2m,weather_code,is_day&timezone=auto";

    HTTPClient http;
    http.setTimeout(8000);
    http.useHTTP10(true);
    if (!http.begin(url)) {
        debugInfo.lastError = "http.begin failed";
        debugInfo.httpCode = -2;
        return false;
    }
    int code = http.GET();
    debugInfo.httpCode = code;
    bool ok = false;
    if (code == 200) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        if (!err) {
            JsonVariant cur = doc["current"];
            if (!cur.isNull()) {
                d.offsetSec = (int)(doc["utc_offset_seconds"] | 0);
                double t = cur["temperature_2m"] | 0.0;
                d.tempC = (int)lround(t);
                d.code = (int)(cur["weather_code"] | 0);
                d.isDay = ((int)(cur["is_day"] | 1)) == 1;
                d.hasData = true;
                ok = true;
                debugInfo.lastError = "";
            } else {
                debugInfo.lastError = "no current field";
            }
        } else {
            debugInfo.lastError = String("parse: ") + err.c_str();
            debugInfo.httpCode = -3;
        }
    } else {
        debugInfo.lastError = String("http ") + code + ": " + http.errorToString(code);
    }
    http.end();
    return ok;
}

bool fetchOneSync(int idx) { return fetchOne(idx); }

void requestRefresh() {
    if (taskHandle) xTaskNotifyGive(taskHandle);
}

// Cache de meteo persistido en LittleFS (cada ~60s) — antes vivía en NVS pero
// las escrituras frecuentes fragmentaban la partición y arrastraban el bug de
// reset post-OTA del cfg blob (ver memoria proyecto). FS aguanta saves
// frecuentes sin degradación.
static const char* WXCACHE_PATH = "/wxcache.json";

void loadCache() {
    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) return;
    File f = LittleFS.open(WXCACHE_PATH, "r");
    if (!f) return;
    String json;
    json.reserve(f.size() + 1);
    while (f.available()) json += (char)f.read();
    f.close();
    if (json.length() == 0) return;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;
    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) return;
    int n = (int)arr.size();
    if (n > 4) n = 4;
    for (int i = 0; i < n; i++) {
        Data& d = data[i];
        d.offsetSec = arr[i]["off"] | 0;
        d.tempC = arr[i]["t"] | 0;
        d.code = arr[i]["c"] | 0;
        d.isDay = arr[i]["d"] | true;
        d.hasData = arr[i]["h"] | false;
    }
    Serial.printf("[weather] cache restored (%d entries)\n", n);
}

void saveCache() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < 4; i++) {
        const Data& d = data[i];
        JsonObject o = arr.add<JsonObject>();
        o["off"] = d.offsetSec;
        o["t"] = d.tempC;
        o["c"] = d.code;
        o["d"] = d.isDay;
        o["h"] = d.hasData;
    }
    String json;
    serializeJson(doc, json);
    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) return;
    File f = LittleFS.open(WXCACHE_PATH, "w");
    if (!f) return;
    f.print(json);
    f.close();
}

Display::IconType::Value iconForCode(int code, bool isDay) {
    using Display::IconType;
    if (code == 0) return isDay ? IconType::SUN : IconType::MOON;
    if (code <= 2) return isDay ? IconType::PARTLY : IconType::PARTLY_NIGHT;
    if (code <= 3) return IconType::CLOUD;
    if (code <= 48) return IconType::FOG;
    if (code <= 67 || (code >= 80 && code <= 82)) return IconType::RAIN;
    if (code <= 77 || (code >= 85 && code <= 86)) return IconType::SNOW;
    if (code >= 95) return IconType::STORM;
    return IconType::CLOUD;
}

static void weatherTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(3000));
    while (true) {
        bool anyOk = false;
        for (int i = 0; i < 4; i++) {
            if (fetchOne(i)) anyOk = true;
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        if (anyOk) saveCache();
        // Espera el intervalo, pero se despierta antes si requestRefresh notifica.
        uint32_t intervalMs = max((uint16_t)30, Config::cfg.weatherRefreshSec) * 1000UL;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(intervalMs));
    }
}

void taskStart() {
    xTaskCreatePinnedToCore(weatherTask, "weather", 8192, nullptr, 1, &taskHandle, 1);
}

}  // namespace Weather
