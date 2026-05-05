#include "WebApi.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>

#include "Config.h"
#include "Display.h"
#include "IndexHtml.h"
#include "Weather.h"
#include "WifiSetup.h"

extern volatile bool g_pendingReset;

namespace WebApi {

static AsyncWebServer server(80);

static const char* modeStr(WifiSetup::Mode m) {
    switch (m) {
        case WifiSetup::Mode::Sta: return "sta";
        case WifiSetup::Mode::Ap: return "ap";
        default: return "none";
    }
}

// Helper: serializar un JsonDocument y enviarlo como respuesta.
static void sendJson(AsyncWebServerRequest* req, JsonDocument& doc, int code = 200) {
    String body;
    serializeJson(doc, body);
    req->send(code, "application/json", body);
}

// Acumula el body en chunks (ESPAsyncWebServer puede partirlo) y llama a `onDone`
// cuando esta completo, con el JsonDocument parseado.
static void accumulateJsonBody(AsyncWebServerRequest* req, uint8_t* data, size_t len,
                                size_t index, size_t total,
                                std::function<void(AsyncWebServerRequest*, JsonDocument&)> onDone) {
    String* body = (String*)req->_tempObject;
    if (!body) {
        body = new String();
        body->reserve(total);
        req->_tempObject = body;
    }
    body->concat((const char*)data, len);
    if (index + len < total) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, *body);
    delete body;
    req->_tempObject = nullptr;
    if (err) {
        req->send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
    }
    onDone(req, doc);
}

static void handleGetStatus(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["wifi_mode"] = modeStr(WifiSetup::currentMode());
    doc["ip"] = WifiSetup::currentIp();
    doc["uptime_sec"] = millis() / 1000;
    doc["heap_free"] = ESP.getFreeHeap();
    doc["psram_free"] = ESP.getFreePsram();
    if (WifiSetup::currentMode() == WifiSetup::Mode::Sta) {
        doc["rssi"] = WiFi.RSSI();
    }
    sendJson(req, doc);
}

static void handleGetWifi(AsyncWebServerRequest* req) {
    JsonDocument doc;
    auto mode = WifiSetup::currentMode();
    doc["mode"] = modeStr(mode);
    doc["current_ssid"] = mode == WifiSetup::Mode::Sta ? WifiSetup::currentSsid() : "";
    doc["ap_ssid"] = mode == WifiSetup::Mode::Ap ? WifiSetup::AP_SSID : "";
    doc["ip"] = WifiSetup::currentIp();
    doc["hostname"] = WifiSetup::currentHostname();
    doc["configured_ssid"] = Config::getWifi().ssid;
    sendJson(req, doc);
}

static void handleGetWifiScan(AsyncWebServerRequest* req) {
    auto rep = WifiSetup::scan();
    JsonDocument doc;
    doc["raw_count"] = rep.rawCount;
    doc["mode_before"] = rep.modeBefore;
    JsonArray arr = doc["networks"].to<JsonArray>();
    for (const auto& n : rep.nets) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = n.ssid;
        o["rssi"] = n.rssi;
        o["secure"] = n.secure;
    }
    sendJson(req, doc);
}

// POST /api/wifi {ssid, password} -> guarda y reinicia.
static void handlePostWifi(AsyncWebServerRequest* req, JsonVariant& body) {
    if (!body.is<JsonObject>()) {
        req->send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
    }
    String ssid = body["ssid"] | "";
    String password = body["password"] | "";
    ssid.trim();
    if (ssid.isEmpty()) {
        req->send(400, "application/json", "{\"error\":\"empty ssid\"}");
        return;
    }
    Config::setWifi(ssid, password);
    req->send(200, "application/json", "{\"ok\":true,\"reload\":true}");
    // Reset diferido para que la respuesta llegue antes.
    static AsyncWebServer* selfRef = &server;
    (void)selfRef;
    delay(200);
    ESP.restart();
}

void begin() {
    // El 4o arg es el partition label. Por defecto espera "spiffs"; nuestra
    // particion se llama "littlefs" (ver gotcha #2 en CLAUDE.md).
    if (!LittleFS.begin(/*formatOnFail=*/true, "/littlefs", 10, "littlefs")) {
        Serial.println("[fs] LittleFS mount FAILED even after format");
    } else {
        // Limpieza one-shot: versiones previas sembraban /index.html en FS para
        // permitir hot-reload via PUT. Ahora servimos siempre el HTML embebido
        // (viaja con el firmware), asi el OTA actualiza UI y no hay drift entre
        // FS y firmware. Borramos el fichero legacy para liberar ~31KB.
        if (LittleFS.exists("/index.html")) {
            LittleFS.remove("/index.html");
            Serial.println("[fs] removed legacy /index.html");
        }
        Serial.printf("[fs] LittleFS ok, used=%u/%u bytes\n",
                      (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
    }

    // /api/config/export ANTES que /api/config: ESPAsyncWebServer matchea por
    // prefix con barra, asi que /api/config atraparia /api/config/export si se
    // registrase primero. Devuelve SOLO el contenido portable de cfg.json
    // (sin rgb_order ni claves NVS).
    server.on("/api/config/export", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        Config::writeBackupJson(doc);
        sendJson(req, doc);
    });

    // ---------- /api/config GET (UI: incluye rgb_order para mostrar) ----------
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        Config::writeJson(doc);
        sendJson(req, doc);
    });

    // ---------- /api/config POST: aplica patch + persiste ----------
    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            accumulateJsonBody(req, data, len, index, total,
                [](AsyncWebServerRequest* req, JsonDocument& doc) {
                    bool citiesChanged = Config::applyPatch(doc);
                    bool saved = Config::save();
                    if (citiesChanged) Weather::requestRefresh();
                    JsonDocument res;
                    res["ok"] = saved;
                    res["cities_changed"] = citiesChanged;
                    if (!saved) res["error"] = Config::diag.lastSaveError;
                    sendJson(req, res);
                });
        });

    // ---------- /api/brightness POST: live, sin escribir NVS ----------
    server.on("/api/brightness", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            accumulateJsonBody(req, data, len, index, total,
                [](AsyncWebServerRequest* req, JsonDocument& doc) {
                    if (doc["brightness"].is<float>()) {
                        float b = constrain((float)doc["brightness"], 0.05f, 1.0f);
                        Config::cfg.brightness = b;
                    }
                    if (doc["night_brightness"].is<float>()) {
                        float b = constrain((float)doc["night_brightness"], 0.05f, 1.0f);
                        Config::cfg.nightMode.brightness = b;
                    }
                    req->send(200, "application/json", "{\"ok\":true}");
                });
        });

    // Detalles de la ultima llamada (URL + body raw) para una ciudad. Util
    // para debugging desde el panel admin. ANTES que /api/weather por matching.
    // Param opcional `provider`: "openmeteo" (default) | "tomorrow"
    server.on("/api/weather/debug", HTTP_GET, [](AsyncWebServerRequest* req) {
        int idx = 0;
        if (req->hasParam("idx")) idx = req->getParam("idx")->value().toInt();
        idx = constrain(idx, 0, 3);
        String provider = "openmeteo";
        if (req->hasParam("provider")) provider = req->getParam("provider")->value();
        const auto& dbg = (provider == "tomorrow") ? Weather::debugInfoTio[idx]
                                                   : Weather::debugInfo[idx];
        JsonDocument doc;
        doc["idx"] = idx;
        doc["provider"] = provider;
        doc["name"] = Config::cfg.cities[idx].name;
        doc["url"] = dbg.lastUrl;
        doc["http"] = dbg.httpCode;
        doc["attempts"] = dbg.attempts;
        doc["last_at_ms"] = dbg.lastAtMs;
        doc["err"] = dbg.lastError;
        doc["body"] = dbg.lastBody;
        doc["body_len"] = dbg.lastBody.length();
        sendJson(req, doc);
    });
    // Trigger sincrono: fuerza un fetch del idx dado y devuelve estado.
    server.on("/api/weather/fetch", HTTP_GET, [](AsyncWebServerRequest* req) {
        int idx = 0;
        if (req->hasParam("idx")) idx = req->getParam("idx")->value().toInt();
        idx = constrain(idx, 0, 3);
        bool ok = Weather::fetchOneSync(idx);
        const auto& dbg = Weather::debugInfo[idx];
        JsonDocument doc;
        doc["ok"] = ok;
        doc["idx"] = idx;
        doc["http"] = dbg.httpCode;
        doc["err"] = dbg.lastError;
        sendJson(req, doc);
    });
    server.on("/api/weather", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["utc_now"] = (uint32_t)time(nullptr);
        JsonArray arr = doc["cities"].to<JsonArray>();
        for (int i = 0; i < 4; i++) {
            const auto& cc = Config::cfg.cities[i];
            const auto& d = Weather::data[i];
            const auto& dbg = Weather::debugInfo[i];
            JsonObject o = arr.add<JsonObject>();
            o["name"] = cc.name;
            o["has_data"] = d.hasData;
            o["offset_sec"] = d.offsetSec;
            o["temp_c"] = d.tempC;
            o["code"] = d.code;
            o["is_day"] = d.isDay;
            o["temp_source"] = (d.tempSource == 2) ? "tomorrow"
                              : (d.tempSource == 1) ? "openmeteo" : "none";
            o["http"] = dbg.httpCode;
            o["attempts"] = dbg.attempts;
            o["last_at_ms"] = dbg.lastAtMs;
            if (dbg.lastError.length()) o["err"] = dbg.lastError;
        }
        sendJson(req, doc);
    });
    server.on("/api/status", HTTP_GET, handleGetStatus);

    // --- Provider meteo Tomorrow.io (NVS, no en backups) ---
    // La api_key se devuelve en plano: el panel admin solo es accesible en LAN
    // y vale más la transparencia para verificar/editar. Si en algún momento
    // ponemos auth en el panel, pensar en mascarar.
    server.on("/api/weather_provider", HTTP_GET, [](AsyncWebServerRequest* req) {
        Config::TomorrowSettings t = Config::getTomorrowSettings();
        JsonDocument doc;
        doc["enabled"] = t.enabled;
        doc["refresh_sec"] = t.refreshSec;
        doc["api_key"] = t.apiKey;
        sendJson(req, doc);
    });
    server.on("/api/weather_provider", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            accumulateJsonBody(req, data, len, index, total,
                [](AsyncWebServerRequest* req, JsonDocument& doc) {
                    Config::TomorrowSettings cur = Config::getTomorrowSettings();
                    bool enabled = doc["enabled"] | cur.enabled;
                    String key = doc["api_key"] | cur.apiKey;
                    uint16_t refresh = cur.refreshSec;
                    if (doc["refresh_sec"].is<int>()) refresh = (uint16_t)(int)doc["refresh_sec"];
                    Config::setTomorrowSettings(enabled, key, refresh);
                    JsonDocument res;
                    res["ok"] = true;
                    res["active"] = enabled && key.length() > 0;
                    sendJson(req, res);
                });
        });

    // Setter dedicado de rgb_order (vive en NVS, separado del cfg).
    server.on("/api/rgb_order", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            accumulateJsonBody(req, data, len, index, total,
                [](AsyncWebServerRequest* req, JsonDocument& doc) {
                    String v = doc["rgb_order"] | String("");
                    bool ok = Config::setRgbOrder(v);
                    JsonDocument res;
                    res["ok"] = ok;
                    if (!ok) res["error"] = "invalid value (RGB|RBG)";
                    sendJson(req, res);
                });
        });

    // Diagnostico NVS (para debug del bug OTA-resetea-config). Devuelve snapshot
    // del estado al boot + delta tras leer config.
    server.on("/api/diag/nvs", HTTP_GET, [](AsyncWebServerRequest* req) {
        Config::captureNvsStatsLive();
        Config::captureFsStatsLive();
        JsonDocument doc;
        const Config::DiagInfo& d = Config::diag;
        doc["boot_count"] = d.bootCount;
        doc["last_load"] = d.lastLoad;
        doc["cfg_blob_len"] = d.cfgBlobLen;
        doc["cfg_string_len"] = d.cfgStringLen;
        doc["wifi_ssid_len"] = d.wifiSsidLen;
        doc["wifi_pwd_len"] = d.wifiPwdLen;
        doc["wxcache_len"] = d.wxCacheLen;
        doc["nvs_used"] = d.nvsUsed;
        doc["nvs_free"] = d.nvsFree;
        doc["nvs_total"] = d.nvsTotal;
        doc["nvs_namespaces"] = d.nvsNamespaces;
        doc["last_save_count"] = d.lastSaveCount;
        doc["last_save_bytes"] = d.lastSaveBytes;
        doc["last_save_wrote"] = d.lastSaveWrote;
        doc["last_save_ok"] = d.lastSaveOk;
        doc["last_save_error"] = d.lastSaveError;
        doc["fs_mounted"] = d.fsMounted;
        doc["fs_cfg_file_len"] = d.fsCfgFileLen;
        doc["fs_total"] = d.fsTotal;
        doc["fs_used"] = d.fsUsed;
        doc["rgb_order"] = Config::cfg.rgbOrder;
        sendJson(req, doc);
    });

    server.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", "{\"ok\":true}");
        delay(200);
        ESP.restart();
    });
    // /api/wifi/scan ANTES que /api/wifi: el matcher de ESPAsyncWebServer hace
    // startsWith con barra, asi que /api/wifi atraparia /api/wifi/scan.
    server.on("/api/wifi/scan", HTTP_GET, handleGetWifiScan);
    server.on("/api/wifi", HTTP_GET, handleGetWifi);

    // POST /api/wifi: usamos AsyncCallbackJsonWebHandler-like approach manual,
    // ESPAsyncWebServer 3.x expone JsonHandler via setBodyParser en algunas
    // versiones; aqui leemos el body raw para no depender de la version.
    server.on("/api/wifi", HTTP_POST,
        /*onRequest=*/[](AsyncWebServerRequest* req) {},
        /*onUpload=*/nullptr,
        /*onBody=*/[](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            accumulateJsonBody(req, data, len, index, total,
                [](AsyncWebServerRequest* req, JsonDocument& doc) {
                    JsonVariant v = doc.as<JsonVariant>();
                    handlePostWifi(req, v);
                });
        });

    // Servir UI siempre desde el binario embebido en flash (IndexHtml.cpp).
    // Asi el HTML viaja con el firmware: cada OTA actualiza UI atomicamente y
    // no hay drift entre filesystem y firmware shipped.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        AsyncWebServerResponse* res = req->beginResponse(
            200, "text/html; charset=utf-8",
            (const uint8_t*)INDEX_HTML, INDEX_HTML_LEN);
        req->send(res);
    });

    // ---------- POST /api/firmware: OTA via subida multipart (form-data) ----------
    // El navegador envia un POST multipart con el .bin como campo de fichero;
    // AsyncWebServer expone los chunks por callback de "upload", lo que da
    // mejor flow-control que el raw body para binarios grandes (~1MB).
    server.on("/api/firmware", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            bool ok = !Update.hasError();
            AsyncWebServerResponse* res = req->beginResponse(
                ok ? 200 : 500, "application/json",
                ok ? "{\"ok\":true}" : "{\"error\":\"update failed\"}");
            res->addHeader("Connection", "close");
            req->send(res);
            if (ok) g_pendingReset = true;
        },
        // onUpload: callback de campo multipart (firmware)
        [](AsyncWebServerRequest* req, const String& filename, size_t index,
           uint8_t* data, size_t len, bool final) {
            static size_t lastLogged = 0;
            if (index == 0) {
                Serial.printf("[ota-http] multipart start: %s\n", filename.c_str());
                if (Update.isRunning()) {
                    Serial.println("[ota-http] aborting stale Update state");
                    Update.abort();
                }
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                    return;
                }
                lastLogged = 0;
            }
            if (len > 0 && Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            // Log de progreso cada ~100KB para distinguir transfer lento vs cuelgue.
            if (index + len - lastLogged >= 100 * 1024) {
                Serial.printf("[ota-http] rx %u KB\n", (unsigned)((index + len) / 1024));
                lastLogged = index + len;
            }
            if (final) {
                Serial.printf("[ota-http] final chunk, total=%u bytes\n",
                              (unsigned)(index + len));
                if (!Update.end(true)) {
                    Update.printError(Serial);
                } else {
                    Serial.printf("[ota-http] flash complete (%u bytes)\n",
                                  (unsigned)(index + len));
                }
            }
        },
        nullptr);

    server.on("/debug/fs", HTTP_GET, [](AsyncWebServerRequest* req) {
        String body;
        body.reserve(512);
        body += "mounted=" + String(LittleFS.totalBytes() ? "yes" : "no") + "\n";
        body += "total=" + String(LittleFS.totalBytes()) + "\n";
        body += "used=" + String(LittleFS.usedBytes()) + "\n";
        body += "files:\n";
        File root = LittleFS.open("/");
        if (root && root.isDirectory()) {
            File f = root.openNextFile();
            while (f) {
                body += "  ";
                body += f.name();
                body += " (";
                body += String(f.size());
                body += " bytes)\n";
                f = root.openNextFile();
            }
        } else {
            body += "  (cannot open /)\n";
        }
        req->send(200, "text/plain", body);
    });

    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "Not found");
    });

    server.begin();
    Serial.println("[http] server started on :80");
}

}  // namespace WebApi
