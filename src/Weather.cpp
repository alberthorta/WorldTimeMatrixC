#include "Weather.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>

#include "Config.h"

namespace Weather {

Data data[4] = {};
FetchDebug debugInfo[4];
FetchDebug debugInfoTio[4];
static TaskHandle_t taskHandle = nullptr;

// Recalcula tempC/code/tempSource/hasData "efectivos" para una ciudad a partir
// de los crudos por proveedor. Reglas:
//   - Si Tomorrow.io esta activo Y la ciudad tiene tio fresh (hasTio && tioOk)
//     → usar valores Tio.
//   - Si no, fallback a Open-Meteo si lo hay.
//   - Si tampoco, marca como sin datos.
static void recomputeEffective(int idx) {
    Data& d = data[idx];
    bool tioActive = Config::hasTomorrowSettings();
    if (tioActive && d.hasTio && d.tioOk) {
        d.tempC = d.tempC_tio;
        d.code = d.code_tio;
        d.tempSource = 2;
        d.hasData = true;
    } else if (d.hasOm) {
        d.tempC = d.tempC_om;
        d.code = d.code_om;
        d.tempSource = 1;
        d.hasData = true;
    } else {
        d.tempSource = 0;
        d.hasData = false;
    }
}

// Mapeo de weatherCode Tomorrow.io (1000-8000) a equivalentes Open-Meteo
// (0-99) para reusar Display::iconForCode sin tocar el render.
static int tomorrowToOpenMeteoCode(int t) {
    switch (t) {
        case 1000: return 0;    // Clear
        case 1100: return 1;    // Mostly Clear
        case 1101: return 2;    // Partly Cloudy
        case 1102: return 3;    // Mostly Cloudy
        case 1001: return 3;    // Cloudy
        case 2000: case 2100: return 45;   // Fog / Light Fog
        case 4000: return 51;   // Drizzle
        case 4200: return 61;   // Light Rain
        case 4001: return 63;   // Rain
        case 4201: return 65;   // Heavy Rain
        case 5001: return 71;   // Flurries
        case 5100: return 73;   // Light Snow
        case 5000: return 75;   // Snow
        case 5101: return 75;   // Heavy Snow
        case 6000: case 6200: return 51;   // Freezing Drizzle / Light Freezing Rain
        case 6001: case 6201: return 65;   // Freezing Rain
        case 7000: case 7101: case 7102: return 75;  // Ice Pellets
        case 8000: return 95;   // Thunderstorm
        default: return 0;
    }
}

static constexpr size_t MAX_DEBUG_BODY = 2048;

static bool fetchOne(int idx) {
    if (idx < 0 || idx >= 4) return false;
    FetchDebug& dbg = debugInfo[idx];
    dbg.attempts++;
    if (!WiFi.isConnected()) {
        dbg.lastError = "no wifi";
        dbg.httpCode = -1;
        dbg.lastAtMs = millis();
        return false;
    }
    const Config::City& cc = Config::cfg.cities[idx];
    Data& d = data[idx];

    String url = "http://api.open-meteo.com/v1/forecast?latitude=";
    url += String(cc.lat, 6);
    url += "&longitude=";
    url += String(cc.lon, 6);
    url += "&current=temperature_2m,weather_code,is_day&timezone=auto";
    dbg.lastUrl = url;
    dbg.lastBody = "";

    HTTPClient http;
    http.setTimeout(8000);
    http.useHTTP10(true);
    if (!http.begin(url)) {
        dbg.lastError = "http.begin failed";
        dbg.httpCode = -2;
        dbg.lastAtMs = millis();
        return false;
    }
    int code = http.GET();
    dbg.httpCode = code;
    bool ok = false;
    // Open-Meteo SIEMPRE actualiza sus campos crudos (om_*); el render decide
    // si los usa (recomputeEffective) según si Tio esté activo y fresh.
    if (code == 200) {
        String body = http.getString();
        if (body.length() > MAX_DEBUG_BODY) {
            dbg.lastBody = body.substring(0, MAX_DEBUG_BODY);
            dbg.lastBody += "\n...[truncated]";
        } else {
            dbg.lastBody = body;
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (!err) {
            JsonVariant cur = doc["current"];
            if (!cur.isNull()) {
                d.offsetSec = (int)(doc["utc_offset_seconds"] | 0);
                d.isDay = ((int)(cur["is_day"] | 1)) == 1;
                double t = cur["temperature_2m"] | 0.0;
                d.tempC_om = (int)lround(t);
                d.code_om = (int)(cur["weather_code"] | 0);
                d.hasOm = true;
                ok = true;
                dbg.lastError = "";
            } else {
                dbg.lastError = "no current field";
            }
        } else {
            dbg.lastError = String("parse: ") + err.c_str();
            dbg.httpCode = -3;
        }
    } else {
        dbg.lastError = String("http ") + code + ": " + http.errorToString(code);
    }
    http.end();
    dbg.lastAtMs = millis();
    recomputeEffective(idx);
    return ok;
}

bool fetchOneSync(int idx) { return fetchOne(idx); }

// Fetch Tomorrow.io realtime para una ciudad. Solo actualiza temp + code en
// data[idx] (NO offset NI isDay — esos los provee Open-Meteo). Requiere
// HTTPS (Tomorrow.io rechaza HTTP). Usa WiFiClientSecure::setInsecure() para
// evitar tener que embedir el cert root; aceptable en este contexto.
static bool fetchTomorrow(int idx) {
    if (idx < 0 || idx >= 4) return false;
    Config::TomorrowSettings tio = Config::getTomorrowSettings();
    if (!tio.enabled || tio.apiKey.length() == 0) return false;

    FetchDebug& dbg = debugInfoTio[idx];
    dbg.attempts++;
    if (!WiFi.isConnected()) {
        dbg.lastError = "no wifi";
        dbg.httpCode = -1;
        dbg.lastAtMs = millis();
        return false;
    }
    const Config::City& cc = Config::cfg.cities[idx];
    Data& d = data[idx];

    String url = "https://api.tomorrow.io/v4/weather/realtime?location=";
    url += String(cc.lat, 6);
    url += ",";
    url += String(cc.lon, 6);
    url += "&units=metric&apikey=";
    url += tio.apiKey;
    dbg.lastUrl = url;
    dbg.lastBody = "";

    WiFiClientSecure client;
    client.setInsecure();   // sin pinning; aceptamos riesgo MITM en LAN
    HTTPClient http;
    http.setTimeout(8000);
    http.useHTTP10(true);
    if (!http.begin(client, url)) {
        dbg.lastError = "http.begin failed";
        dbg.httpCode = -2;
        dbg.lastAtMs = millis();
        return false;
    }
    int code = http.GET();
    dbg.httpCode = code;
    bool ok = false;
    if (code == 200) {
        String body = http.getString();
        if (body.length() > MAX_DEBUG_BODY) {
            dbg.lastBody = body.substring(0, MAX_DEBUG_BODY) + "\n...[truncated]";
        } else {
            dbg.lastBody = body;
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (!err) {
            JsonVariant vals = doc["data"]["values"];
            if (!vals.isNull()) {
                double t = vals["temperature"] | 0.0;
                int tcode = (int)(vals["weatherCode"] | 0);
                d.tempC_tio = (int)lround(t);
                d.code_tio = tomorrowToOpenMeteoCode(tcode);
                d.hasTio = true;
                d.tioOk = true;     // exito → esta ciudad usa Tio hasta proximo fallo
                ok = true;
                dbg.lastError = "";
            } else {
                dbg.lastError = "no data.values field";
            }
        } else {
            dbg.lastError = String("parse: ") + err.c_str();
            dbg.httpCode = -3;
        }
    } else {
        // Tomorrow.io devuelve detalles utiles en el body (e.g., 401 invalid
        // key, 429 rate limit) — los capturamos para el modal de debug.
        String body = http.getString();
        if (body.length() > MAX_DEBUG_BODY) {
            dbg.lastBody = body.substring(0, MAX_DEBUG_BODY) + "\n...[truncated]";
        } else {
            dbg.lastBody = body;
        }
        dbg.lastError = String("http ") + code + ": " + http.errorToString(code);
    }
    http.end();
    if (!ok) {
        // Fallo (red, parse, 401, rate limit, ...): marca tio como stale para
        // esta ciudad. recomputeEffective hara que use OM como fallback hasta
        // que la rotacion vuelva y consiga un fetch ok.
        d.tioOk = false;
    }
    dbg.lastAtMs = millis();
    recomputeEffective(idx);
    return ok;
}

bool fetchOneTomorrowSync(int idx) { return fetchTomorrow(idx); }

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
        d.isDay = arr[i]["d"] | true;
        // Crudos por proveedor (compatibilidad con cache antiguo: si no hay
        // om_t pero sí t/c y s==1, usa esos como om).
        if (arr[i]["om_t"].is<int>()) {
            d.tempC_om = arr[i]["om_t"];
            d.code_om = arr[i]["om_c"] | 0;
            d.hasOm = arr[i]["om_h"] | false;
        } else if ((int)(arr[i]["s"] | 0) == 1) {
            d.tempC_om = arr[i]["t"] | 0;
            d.code_om = arr[i]["c"] | 0;
            d.hasOm = arr[i]["h"] | false;
        }
        if (arr[i]["tio_t"].is<int>()) {
            d.tempC_tio = arr[i]["tio_t"];
            d.code_tio = arr[i]["tio_c"] | 0;
            d.hasTio = arr[i]["tio_h"] | false;
            d.tioOk = arr[i]["tio_ok"] | false;
        } else if ((int)(arr[i]["s"] | 0) == 2) {
            d.tempC_tio = arr[i]["t"] | 0;
            d.code_tio = arr[i]["c"] | 0;
            d.hasTio = arr[i]["h"] | false;
            d.tioOk = false;   // no sabemos si era fresh, conservador
        }
        recomputeEffective(i);
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
        o["d"] = d.isDay;
        // Crudos por proveedor.
        if (d.hasOm) {
            o["om_t"] = d.tempC_om;
            o["om_c"] = d.code_om;
            o["om_h"] = true;
        }
        if (d.hasTio) {
            o["tio_t"] = d.tempC_tio;
            o["tio_c"] = d.code_tio;
            o["tio_h"] = true;
            o["tio_ok"] = d.tioOk;
        }
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
    // Tomorrow.io rota una ciudad por tick (cada refresh_sec). lastTioMs=0
    // significa "aun no ha hecho fetch nunca" → primer tick lo hace inmediato.
    uint32_t lastTioMs = 0;
    int tioRotIdx = 0;
    bool tioWasActive = false;
    while (true) {
        // Open-Meteo: ronda completa siempre (provee offset + isDay; temp+code
        // solo si Tomorrow.io NO esta activo — esa logica vive en fetchOne).
        bool anyOk = false;
        for (int i = 0; i < 4; i++) {
            if (fetchOne(i)) anyOk = true;
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        // Tomorrow.io: 1 ciudad por tick rotando 0->1->2->3->0... A los X
        // segundos definidos. Si se acaba de activar (era false antes), reset
        // del temporizador para forzar primer fetch inmediato.
        Config::TomorrowSettings tio = Config::getTomorrowSettings();
        bool tioActive = tio.enabled && tio.apiKey.length() > 0;
        if (tioActive && !tioWasActive) {
            lastTioMs = 0;        // forzar fetch inmediato al activarse
            tioRotIdx = 0;
        }
        tioWasActive = tioActive;
        if (tioActive) {
            uint32_t now = millis();
            uint32_t intervalMs = max((uint16_t)60, tio.refreshSec) * 1000UL;
            if (lastTioMs == 0 || (now - lastTioMs) >= intervalMs) {
                if (fetchTomorrow(tioRotIdx)) anyOk = true;
                tioRotIdx = (tioRotIdx + 1) % 4;
                lastTioMs = millis();
            }
        }
        if (anyOk) saveCache();
        uint32_t intervalMs = max((uint16_t)30, Config::cfg.weatherRefreshSec) * 1000UL;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(intervalMs));
    }
}

void taskStart() {
    xTaskCreatePinnedToCore(weatherTask, "weather", 8192, nullptr, 1, &taskHandle, 1);
}

}  // namespace Weather
