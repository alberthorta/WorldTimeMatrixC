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
FetchDebug debugInfoWap[4];
static TaskHandle_t taskHandle = nullptr;

// Indice de la proxima ciudad que la rotacion del provider premium activo
// (TIO o WAP) va a fetchar. Se expone via nextPremiumIdx() para que la UI
// pueda pintar el orden de actualizacion. Cuando no hay provider activo el
// valor no es relevante (la UI lo oculta).
static int g_premiumRotIdx = 0;

int nextPremiumIdx() { return g_premiumRotIdx; }

// Edad maxima admitida de un fetch premium con exito para considerarlo "fresh".
// Mas viejo que esto y la ciudad cae a OM aunque el ultimo intento fuera ok.
// Cubre el caso de que la rotacion sea muy lenta (e.g. 4h) y los datos queden
// obsoletos entre vueltas. Misma regla para TIO y WAP.
static const uint32_t PREMIUM_MAX_AGE_MS = 60UL * 60UL * 1000UL;   // 1 hora

// Recalcula tempC/code/tempSource/hasData "efectivos" para una ciudad a partir
// de los crudos por proveedor. Reglas:
//   - Si hay un provider premium activo (TIO o WAP) Y la ciudad tiene crudos
//     suyos fresh (hasX && xOk && fetch ok hace < 1h) → usar esos valores.
//   - Si no, fallback a Open-Meteo si lo hay.
//   - Si tampoco, marca como sin datos.
static void recomputeEffective(int idx) {
    Data& d = data[idx];
    Config::PremiumProvider active = Config::activePremium();
    bool tioFresh = (d.tioOkAtMs != 0) &&
                    (millis() - d.tioOkAtMs) < PREMIUM_MAX_AGE_MS;
    bool wapFresh = (d.wapOkAtMs != 0) &&
                    (millis() - d.wapOkAtMs) < PREMIUM_MAX_AGE_MS;
    if (active == Config::PremiumProvider::TOMORROW &&
        d.hasTio && d.tioOk && tioFresh) {
        d.tempC = d.tempC_tio;
        d.code = d.code_tio;
        d.tempSource = 2;
        d.hasData = true;
    } else if (active == Config::PremiumProvider::WEATHERAPI &&
               d.hasWap && d.wapOk && wapFresh) {
        d.tempC = d.tempC_wap;
        d.code = d.code_wap;
        d.tempSource = 3;
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

// Mapeo de weatherCode WeatherAPI (1000-1282) a equivalentes Open-Meteo (0-99)
// para reusar iconForCode sin tocar el render. La taxonomia WAP es mas fina
// que OM, varios codigos colapsan al mismo equivalente OM.
static int weatherApiToOpenMeteoCode(int w) {
    switch (w) {
        case 1000: return 0;    // Sunny / Clear
        case 1003: return 2;    // Partly cloudy
        case 1006: return 3;    // Cloudy
        case 1009: return 3;    // Overcast
        case 1030: return 45;   // Mist
        case 1063: return 61;   // Patchy rain possible
        case 1066: return 71;   // Patchy snow possible
        case 1069: case 1072: return 67;   // Patchy sleet / freezing drizzle
        case 1087: return 95;   // Thundery outbreaks
        case 1114: return 73;   // Blowing snow
        case 1117: return 75;   // Blizzard
        case 1135: return 45;   // Fog
        case 1147: return 48;   // Freezing fog
        case 1150: case 1153: return 51;   // Light drizzle
        case 1168: case 1171: return 56;   // Freezing drizzle
        case 1180: case 1183: return 61;   // Light rain
        case 1186: case 1189: return 63;   // Moderate rain
        case 1192: case 1195: return 65;   // Heavy rain
        case 1198: case 1201: return 67;   // Light/moderate freezing rain
        case 1204: case 1207: return 67;   // Sleet
        case 1210: case 1213: return 71;   // Light snow
        case 1216: case 1219: return 73;   // Moderate snow
        case 1222: case 1225: return 75;   // Heavy snow
        case 1237: return 77;              // Ice pellets
        case 1240: return 80;              // Light rain shower
        case 1243: return 81;              // Moderate rain shower
        case 1246: return 82;              // Torrential rain shower
        case 1249: case 1252: return 67;   // Sleet showers
        case 1255: case 1258: return 85;   // Snow showers
        case 1261: case 1264: return 86;   // Heavy snow showers
        case 1273: case 1276: return 95;   // Thundery rain
        case 1279: case 1282: return 95;   // Thundery snow
        default: return 3;                 // fallback nuboso
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
                d.tioOkAtMs = millis();   // ancla de frescura (ver TIO_MAX_AGE_MS)
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

// Fetch WeatherAPI current.json para una ciudad. Misma logica estructural que
// fetchTomorrow: solo actualiza temp + code en data[idx] (NO offset ni isDay,
// que vienen de Open-Meteo). HTTPS via WiFiClientSecure::setInsecure().
static bool fetchWeatherApi(int idx) {
    if (idx < 0 || idx >= 4) return false;
    Config::WeatherApiSettings wap = Config::getWeatherApiSettings();
    if (!wap.enabled || wap.apiKey.length() == 0) return false;

    FetchDebug& dbg = debugInfoWap[idx];
    dbg.attempts++;
    if (!WiFi.isConnected()) {
        dbg.lastError = "no wifi";
        dbg.httpCode = -1;
        dbg.lastAtMs = millis();
        return false;
    }
    const Config::City& cc = Config::cfg.cities[idx];
    Data& d = data[idx];

    String url = "https://api.weatherapi.com/v1/current.json?key=";
    url += wap.apiKey;
    url += "&q=";
    url += String(cc.lat, 6);
    url += ",";
    url += String(cc.lon, 6);
    url += "&aqi=no";
    dbg.lastUrl = url;
    dbg.lastBody = "";

    WiFiClientSecure client;
    client.setInsecure();
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
            JsonVariant cur = doc["current"];
            if (!cur.isNull()) {
                double t = cur["temp_c"] | 0.0;
                int wcode = (int)(cur["condition"]["code"] | 0);
                d.tempC_wap = (int)lround(t);
                d.code_wap = weatherApiToOpenMeteoCode(wcode);
                d.hasWap = true;
                d.wapOk = true;
                d.wapOkAtMs = millis();
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
        // WeatherAPI devuelve {"error":{"code":N,"message":"..."}} en fallos
        // — capturamos el body para el modal de debug.
        String body = http.getString();
        if (body.length() > MAX_DEBUG_BODY) {
            dbg.lastBody = body.substring(0, MAX_DEBUG_BODY) + "\n...[truncated]";
        } else {
            dbg.lastBody = body;
        }
        dbg.lastError = String("http ") + code + ": " + http.errorToString(code);
    }
    http.end();
    if (!ok) d.wapOk = false;
    dbg.lastAtMs = millis();
    recomputeEffective(idx);
    return ok;
}

bool fetchOneWeatherApiSync(int idx) { return fetchWeatherApi(idx); }

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
        if (arr[i]["wap_t"].is<int>()) {
            d.tempC_wap = arr[i]["wap_t"];
            d.code_wap = arr[i]["wap_c"] | 0;
            d.hasWap = arr[i]["wap_h"] | false;
            d.wapOk = arr[i]["wap_ok"] | false;
        }
        // Tras reboot, los timestamps de frescura quedan en 0 (default Data).
        // Hasta que la rotacion vuelva a hacer un fetch ok, recomputeEffective
        // tratara los datos premium como stale y la fila ira por OM.
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
        if (d.hasWap) {
            o["wap_t"] = d.tempC_wap;
            o["wap_c"] = d.code_wap;
            o["wap_h"] = true;
            o["wap_ok"] = d.wapOk;
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
    // Provider premium: rota una ciudad por tick (cada refresh_sec del provider
    // activo). lastPremiumMs=0 = "aun no ha hecho fetch nunca" → primer tick
    // inmediato. Si cambia el provider activo (NONE→TIO, TIO→WAP, etc.) reset
    // de temporizador y rotIdx para arrancar en idx 0.
    uint32_t lastPremiumMs = 0;
    Config::PremiumProvider lastActive = Config::PremiumProvider::NONE;
    while (true) {
        // Open-Meteo: ronda completa siempre (provee offset + isDay; temp+code
        // solo si NO hay premium activo — esa logica vive en recomputeEffective).
        bool anyOk = false;
        for (int i = 0; i < 4; i++) {
            if (fetchOne(i)) anyOk = true;
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        Config::PremiumProvider active = Config::activePremium();
        if (active != lastActive) {
            // Toggle del provider (incluye on/off y switch entre TIO↔WAP):
            // resetea rotacion para empezar en idx 0 con fetch inmediato.
            lastPremiumMs = 0;
            g_premiumRotIdx = 0;
        }
        lastActive = active;
        uint16_t refreshSec = 60;
        if (active == Config::PremiumProvider::TOMORROW) {
            refreshSec = Config::getTomorrowSettings().refreshSec;
        } else if (active == Config::PremiumProvider::WEATHERAPI) {
            refreshSec = Config::getWeatherApiSettings().refreshSec;
        }
        if (active != Config::PremiumProvider::NONE) {
            uint32_t now = millis();
            uint32_t intervalMs = max((uint16_t)60, refreshSec) * 1000UL;
            if (lastPremiumMs == 0 || (now - lastPremiumMs) >= intervalMs) {
                bool premiumOk = false;
                if (active == Config::PremiumProvider::TOMORROW) {
                    premiumOk = fetchTomorrow(g_premiumRotIdx);
                } else {
                    premiumOk = fetchWeatherApi(g_premiumRotIdx);
                }
                if (premiumOk) anyOk = true;
                g_premiumRotIdx = (g_premiumRotIdx + 1) % 4;
                lastPremiumMs = millis();
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
