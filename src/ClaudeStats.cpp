#include "ClaudeStats.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "Config.h"

namespace ClaudeStats {

Data data;

static const char* CACHE_PATH = "/claudecache.json";
static TaskHandle_t s_task = nullptr;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static uint32_t toRgb888(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

bool isConfigured() {
    return Config::cfg.claudeSessionKey.length() > 0;
}

Pace computePace(const UsageWindow& w, long totalSeconds, time_t now) {
    Pace p;
    if (!w.valid) return p;
    p.used = w.utilization / 100.0;
    if (p.used < 0) p.used = 0;
    if (p.used > 1) p.used = 1;

    if (w.resetsAt > 0 && now > 0 && totalSeconds > 0) {
        double remaining = (double)(w.resetsAt - now);
        if (remaining < 0) remaining = 0;
        if (remaining > (double)totalSeconds) remaining = totalSeconds;
        p.elapsed = 1.0 - remaining / (double)totalSeconds;
        if (p.elapsed < 0) p.elapsed = 0;
        if (p.elapsed > 1) p.elapsed = 1;
    }
    p.ratio = p.elapsed > 0.01 ? p.used / p.elapsed : 0;

    // Mismos colores y thresholds que ClaudeStatsPortable y la app macOS.
    if (p.ratio < 0.75)      { p.label = "Well under"; p.color = toRgb888( 76, 175,  80); }
    else if (p.ratio < 0.95) { p.label = "Under";      p.color = toRgb888( 87, 217, 163); }
    else if (p.ratio < 1.10) { p.label = "On pace";    p.color = toRgb888(255, 213,   0); }
    else if (p.ratio < 1.35) { p.label = "Over";       p.color = toRgb888(255, 152,   0); }
    else                     { p.label = "Burning";    p.color = toRgb888(244,  67,  54); }
    return p;
}

String formatCountdown(time_t resetsAt, time_t now, bool isWeekly) {
    if (resetsAt == 0 || now == 0) return "-";
    long remaining = (long)(resetsAt - now);
    if (remaining < 0) remaining = 0;
    char buf[16];
    if (isWeekly) {
        long days  = remaining / 86400;
        long hours = (remaining % 86400) / 3600;
        snprintf(buf, sizeof(buf), "%ldd%ldh", days, hours);
    } else {
        long hours = remaining / 3600;
        long mins  = (remaining % 3600) / 60;
        snprintf(buf, sizeof(buf), "%ldh%02ldm", hours, mins);
    }
    return String(buf);
}

static time_t parseIso8601Utc(const char* s) {
    if (!s || !*s) return 0;
    int y, mo, d, h, mi, se;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
    struct tm tm = {};
    tm.tm_year = y - 1900;
    tm.tm_mon  = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min  = mi;
    tm.tm_sec  = se;
    // El proyecto usa UTC en time() (configTime con offset 0). mktime
    // interpretara la struct como local time, pero como TZ=UTC (default si
    // no se llama a setenv("TZ", ...)) el resultado es epoch UTC.
    return mktime(&tm);
}

// Auto-descubre orgId desde /api/organizations. Guarda en Config y persiste.
// Devuelve true si tras la llamada hay orgId disponible.
static bool ensureOrgId() {
    if (Config::cfg.claudeOrgId.length() > 0) return true;
    if (Config::cfg.claudeSessionKey.length() == 0) return false;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(10000);
    if (!http.begin(client, "https://claude.ai/api/organizations")) {
        data.lastError = "orgid: begin failed";
        return false;
    }
    http.addHeader("Cookie", "sessionKey=" + Config::cfg.claudeSessionKey);
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "WorldTimeMatrixC/1.0 (ESP32-S3)");
    int code = http.GET();
    if (code != 200) {
        data.lastError = "orgid http " + String(code);
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body)) { data.lastError = "orgid parse"; return false; }
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull() || arr.size() == 0) { data.lastError = "no orgs"; return false; }
    const char* uuid = arr[0]["uuid"] | (const char*)nullptr;
    if (!uuid || !*uuid) { data.lastError = "no uuid"; return false; }
    Config::cfg.claudeOrgId = uuid;
    Config::save();
    Serial.printf("[claude] discovered orgId=%s\n", uuid);
    return true;
}

static bool fetchUsageOnce() {
    if (!ensureOrgId()) return false;
    if (WiFi.status() != WL_CONNECTED) {
        data.lastError = "no wifi";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(10000);
    String url = "https://claude.ai/api/organizations/" + Config::cfg.claudeOrgId + "/usage";
    if (!http.begin(client, url)) { data.lastError = "usage: begin failed"; return false; }
    http.addHeader("Cookie", "sessionKey=" + Config::cfg.claudeSessionKey);
    http.addHeader("Accept", "application/json");
    http.addHeader("User-Agent", "WorldTimeMatrixC/1.0 (ESP32-S3)");
    int code = http.GET();
    if (code == 401 || code == 403) {
        // sessionKey expirada o orgId invalido. Invalidamos orgId para que
        // el siguiente intento lo redescubra.
        Config::cfg.claudeOrgId = "";
        Config::save();
        data.lastError = "auth (sessionKey?)";
        http.end();
        return false;
    }
    if (code != 200) {
        data.lastError = "http " + String(code);
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body)) { data.lastError = "usage parse"; return false; }

    UsageWindow five, seven;
    auto parseWindow = [](JsonVariantConst v, UsageWindow& w) {
        if (v.isNull()) return;
        w.valid = true;
        if (!v["utilization"].isNull()) w.utilization = v["utilization"].as<double>();
        const char* iso = v["resets_at"] | (const char*)nullptr;
        w.resetsAt = parseIso8601Utc(iso);
    };
    parseWindow(doc["five_hour"], five);
    parseWindow(doc["seven_day"], seven);

    data.fiveHour = five;
    data.sevenDay = seven;
    data.hasData = five.valid || seven.valid;
    data.lastOkAtMs = millis();
    data.lastError = "";
    return true;
}

void loadCache() {
    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) return;
    if (!LittleFS.exists(CACHE_PATH)) return;
    File f = LittleFS.open(CACHE_PATH, "r");
    if (!f) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;
    auto loadWindow = [](JsonVariantConst v, UsageWindow& w) {
        if (v.isNull()) return;
        w.valid       = v["valid"] | false;
        w.utilization = v["utilization"] | 0.0;
        w.resetsAt    = (time_t)(v["resets_at"] | 0);
    };
    loadWindow(doc["five_hour"], data.fiveHour);
    loadWindow(doc["seven_day"], data.sevenDay);
    data.hasData = data.fiveHour.valid || data.sevenDay.valid;
}

void saveCache() {
    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) return;
    JsonDocument doc;
    auto saveWindow = [](JsonObject o, const UsageWindow& w) {
        o["valid"]       = w.valid;
        o["utilization"] = w.utilization;
        o["resets_at"]   = (uint32_t)w.resetsAt;
    };
    saveWindow(doc["five_hour"].to<JsonObject>(), data.fiveHour);
    saveWindow(doc["seven_day"].to<JsonObject>(), data.sevenDay);
    File f = LittleFS.open(CACHE_PATH, "w");
    if (!f) return;
    serializeJson(doc, f);
    f.close();
}

static void taskBody(void*) {
    // Espera inicial para no saturar el boot con concurrent HTTP.
    vTaskDelay(pdMS_TO_TICKS(8000));
    for (;;) {
        if (isConfigured()) {
            bool ok = fetchUsageOnce();
            if (ok) {
                Serial.printf("[claude] ok: 5h=%.1f%% 7d=%.1f%%\n",
                              data.fiveHour.valid ? data.fiveHour.utilization : -1,
                              data.sevenDay.valid ? data.sevenDay.utilization : -1);
                saveCache();
            } else {
                Serial.printf("[claude] fetch fail: %s\n", data.lastError.c_str());
            }
        }
        // Espera notificacion (requestRefresh) o timeout = refresh_sec.
        uint32_t waitMs = (uint32_t)Config::cfg.claudeRefreshSec * 1000UL;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
    }
}

void taskStart() {
    if (s_task) return;
    xTaskCreatePinnedToCore(taskBody, "claude", 8192, nullptr, 1, &s_task, 1);
}

void requestRefresh() {
    if (s_task) xTaskNotifyGive(s_task);
}

}  // namespace ClaudeStats
