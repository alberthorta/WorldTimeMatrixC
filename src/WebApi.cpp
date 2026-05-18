#include "WebApi.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>

#include "AutoUpdate.h"
#include "Config.h"
#include "Display.h"
#include "IndexHtml.h"
#include "MoonPhase.h"

// Definida en main.cpp (sin namespace). Forward decl global.
extern void requestButtonPress(int idx);
#include "Version.h"
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
    doc["fw_version"] = FW_VERSION;
    doc["wifi_mode"] = modeStr(WifiSetup::currentMode());
    doc["ip"] = WifiSetup::currentIp();
    doc["uptime_sec"] = millis() / 1000;
    doc["heap_free"] = ESP.getFreeHeap();
    doc["psram_free"] = ESP.getFreePsram();
    if (WifiSetup::currentMode() == WifiSetup::Mode::Sta) {
        doc["rssi"] = WiFi.RSSI();
    }
    // Fase lunar actual (util para verificar el calculo y debugear la seleccion
    // de variante de icono MOON/PARTLY_NIGHT). Solo informativo si hay NTP sync.
    time_t now = time(nullptr);
    if (now > 1700000000) {
        double age = MoonPhase::ageDays(now);
        MoonPhase::Phase p = MoonPhase::bucketize(age);
        JsonObject moon = doc["moon"].to<JsonObject>();
        moon["age_days"] = age;
        moon["phase"] = MoonPhase::name(p);
        moon["synodic_days"] = MoonPhase::SYNODIC_DAYS;
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
    // Connection: close en TODAS las respuestas — sin keepalive. Tras un OTA
    // reset, los sockets idle del navegador no quedan apuntando a un puerto
    // muerto (lo que causaba que la web tarde en responder al recargar).
    // Coste por request en LAN es despreciable.
    DefaultHeaders::Instance().addHeader("Connection", "close");

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
    // Param opcional `provider`: "openmeteo" (default) | "tomorrow" | "weatherapi"
    server.on("/api/weather/debug", HTTP_GET, [](AsyncWebServerRequest* req) {
        int idx = 0;
        if (req->hasParam("idx")) idx = req->getParam("idx")->value().toInt();
        idx = constrain(idx, 0, 3);
        String provider = "openmeteo";
        if (req->hasParam("provider")) provider = req->getParam("provider")->value();
        const auto& dbg = (provider == "tomorrow")   ? Weather::debugInfoTio[idx]
                        : (provider == "weatherapi") ? Weather::debugInfoWap[idx]
                                                     : Weather::debugInfo[idx];
        JsonDocument doc;
        doc["idx"] = idx;
        doc["provider"] = provider;
        doc["name"] = Config::cfg.cities[idx].name;
        doc["url"] = dbg.lastUrl;
        doc["http"] = dbg.httpCode;
        doc["attempts"] = dbg.attempts;
        // age_ms = "hace cuanto fue el ultimo fetch" calculado en el device,
        // ya que last_at_ms es millis() (relativo al boot) y no se puede
        // comparar con el reloj del navegador.
        doc["age_ms"] = dbg.lastAtMs ? (uint32_t)(millis() - dbg.lastAtMs) : 0;
        doc["last_at_ms"] = dbg.lastAtMs;
        doc["ok_age_ms"] = dbg.lastOkAtMs ? (uint32_t)(millis() - dbg.lastOkAtMs) : 0;
        doc["last_ok_at_ms"] = dbg.lastOkAtMs;
        doc["err"] = dbg.lastError;
        doc["body"] = dbg.lastBody;
        doc["body_len"] = dbg.lastBody.length();
        sendJson(req, doc);
    });
    // Trigger sincrono: fuerza un fetch del idx dado y devuelve estado.
    // Param opcional `provider`: "openmeteo" (default) | "tomorrow" | "weatherapi"
    server.on("/api/weather/fetch", HTTP_GET, [](AsyncWebServerRequest* req) {
        int idx = 0;
        if (req->hasParam("idx")) idx = req->getParam("idx")->value().toInt();
        idx = constrain(idx, 0, 3);
        String provider = "openmeteo";
        if (req->hasParam("provider")) provider = req->getParam("provider")->value();
        bool ok;
        const Weather::FetchDebug* dbgPtr;
        if (provider == "tomorrow") {
            ok = Weather::fetchOneTomorrowSync(idx);
            dbgPtr = &Weather::debugInfoTio[idx];
        } else if (provider == "weatherapi") {
            ok = Weather::fetchOneWeatherApiSync(idx);
            dbgPtr = &Weather::debugInfoWap[idx];
        } else {
            ok = Weather::fetchOneSync(idx);
            dbgPtr = &Weather::debugInfo[idx];
        }
        const auto& dbg = *dbgPtr;
        JsonDocument doc;
        doc["ok"] = ok;
        doc["idx"] = idx;
        doc["provider"] = provider;
        doc["http"] = dbg.httpCode;
        doc["err"] = dbg.lastError;
        sendJson(req, doc);
    });
    server.on("/api/weather", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["utc_now"] = (uint32_t)time(nullptr);
        Config::PremiumProvider active = Config::activePremium();
        const char* activeName = (active == Config::PremiumProvider::TOMORROW)   ? "tomorrow"
                               : (active == Config::PremiumProvider::WEATHERAPI) ? "weatherapi"
                                                                                 : "none";
        doc["premium_active"] = (active != Config::PremiumProvider::NONE);
        doc["premium_provider"] = activeName;
        doc["premium_next_idx"] = Weather::nextPremiumIdx();
        // Aliases historicos para retro-compat de la UI mientras se actualiza.
        doc["tomorrow_active"] = (active == Config::PremiumProvider::TOMORROW);
        doc["tio_next_idx"] = Weather::nextPremiumIdx();
        JsonArray arr = doc["cities"].to<JsonArray>();
        uint32_t now = millis();
        for (int i = 0; i < 4; i++) {
            const auto& cc = Config::cfg.cities[i];
            const auto& d = Weather::data[i];
            const auto& dbg = Weather::debugInfo[i];
            const auto& dbgTio = Weather::debugInfoTio[i];
            const auto& dbgWap = Weather::debugInfoWap[i];
            JsonObject o = arr.add<JsonObject>();
            o["name"] = cc.name;
            o["has_data"] = d.hasData;
            o["offset_sec"] = d.offsetSec;
            o["temp_c"] = d.tempC;
            o["code"] = d.code;
            o["is_day"] = d.isDay;
            o["temp_source"] = (d.tempSource == 3) ? "weatherapi"
                              : (d.tempSource == 2) ? "tomorrow"
                              : (d.tempSource == 1) ? "openmeteo" : "none";
            o["http"] = dbg.httpCode;
            o["attempts"] = dbg.attempts;
            o["last_at_ms"] = dbg.lastAtMs;
            // Edad en segundos desde el ultimo fetch de cada proveedor; -1 si nunca.
            o["om_age_s"]  = (dbg.lastAtMs    == 0) ? -1 : (int32_t)((now - dbg.lastAtMs)    / 1000U);
            o["tio_age_s"] = (dbgTio.lastAtMs == 0) ? -1 : (int32_t)((now - dbgTio.lastAtMs) / 1000U);
            o["wap_age_s"] = (dbgWap.lastAtMs == 0) ? -1 : (int32_t)((now - dbgWap.lastAtMs) / 1000U);
            if (dbg.lastError.length()) o["err"] = dbg.lastError;
        }
        sendJson(req, doc);
    });
    server.on("/api/status", HTTP_GET, handleGetStatus);

    // --- Preview de icono en device ---
    // /api/icons/preview/stop ANTES que /api/icons/preview por prefix matching.
    server.on("/api/icons/preview/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
        Display::clearIconPreview();
        req->send(200, "application/json", "{\"ok\":true}");
    });
    // POST /api/icons/preview con body {frames:[{px:[[..]],ms:500},...], duration_ms:60000}
    // Sobrescribe el icono de la fila 0 con los frames recibidos. Util para
    // testear iconos en edicion sin tener que guardar la config.
    server.on("/api/icons/preview", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            accumulateJsonBody(req, data, len, index, total,
                [](AsyncWebServerRequest* req, JsonDocument& doc) {
                    JsonArray arr = doc["frames"].as<JsonArray>();
                    std::vector<Icons::Frame> frames;
                    if (!arr.isNull()) {
                        for (JsonObject f : arr) {
                            Icons::Frame fr{};
                            fr.ms = f["ms"] | 500;
                            JsonArray rows = f["px"].as<JsonArray>();
                            if (!rows.isNull()) {
                                for (int y = 0; y < Icons::ICON_H && y < (int)rows.size(); y++) {
                                    JsonArray cols = rows[y].as<JsonArray>();
                                    if (cols.isNull()) continue;
                                    for (int x = 0; x < Icons::ICON_W && x < (int)cols.size(); x++) {
                                        fr.px[y][x] = (uint8_t)((int)cols[x] & 0xFF);
                                    }
                                }
                            }
                            frames.push_back(fr);
                        }
                    }
                    uint32_t dur = doc["duration_ms"] | 60000U;   // failsafe 60s
                    Display::setIconPreview(frames, dur);
                    JsonDocument res;
                    res["ok"] = !frames.empty();
                    res["frames"] = (int)frames.size();
                    res["duration_ms"] = dur;
                    sendJson(req, res);
                });
        });

    // --- Provider meteo premium (Tomorrow.io / WeatherAPI, NVS, no en backups) ---
    // Solo uno puede estar activo a la vez (exclusion mutua en los setters).
    // Las api_keys se devuelven en plano: panel admin solo en LAN y vale mas
    // la transparencia para verificar/editar. Si añadimos auth, mascarar.
    server.on("/api/weather_provider", HTTP_GET, [](AsyncWebServerRequest* req) {
        Config::TomorrowSettings t = Config::getTomorrowSettings();
        Config::WeatherApiSettings w = Config::getWeatherApiSettings();
        Config::PremiumProvider act = Config::activePremium();
        JsonDocument doc;
        doc["active"] = (act == Config::PremiumProvider::TOMORROW)   ? "tomorrow"
                       : (act == Config::PremiumProvider::WEATHERAPI) ? "weatherapi"
                                                                      : "none";
        JsonObject jt = doc["tomorrow"].to<JsonObject>();
        jt["enabled"] = t.enabled;
        jt["refresh_sec"] = t.refreshSec;
        jt["api_key"] = t.apiKey;
        JsonObject jw = doc["weatherapi"].to<JsonObject>();
        jw["enabled"] = w.enabled;
        jw["refresh_sec"] = w.refreshSec;
        jw["api_key"] = w.apiKey;
        sendJson(req, doc);
    });
    // POST acepta:
    //   { "active": "none"|"tomorrow"|"weatherapi",
    //     "tomorrow":   { "api_key":"...", "refresh_sec": N },
    //     "weatherapi": { "api_key":"...", "refresh_sec": N } }
    // Cualquiera de los sub-objetos es opcional; si faltan campos se mantienen
    // los valores actuales. `active` decide cual queda enabled (los otros van
    // a enabled=false). Las claves persisten aunque el provider este off.
    server.on("/api/weather_provider", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            accumulateJsonBody(req, data, len, index, total,
                [](AsyncWebServerRequest* req, JsonDocument& doc) {
                    String active = doc["active"] | String("none");
                    Config::TomorrowSettings   curT = Config::getTomorrowSettings();
                    Config::WeatherApiSettings curW = Config::getWeatherApiSettings();
                    String tioKey = doc["tomorrow"]["api_key"]   | curT.apiKey;
                    String wapKey = doc["weatherapi"]["api_key"] | curW.apiKey;
                    uint16_t tioRef = curT.refreshSec, wapRef = curW.refreshSec;
                    if (doc["tomorrow"]["refresh_sec"].is<int>())
                        tioRef = (uint16_t)(int)doc["tomorrow"]["refresh_sec"];
                    if (doc["weatherapi"]["refresh_sec"].is<int>())
                        wapRef = (uint16_t)(int)doc["weatherapi"]["refresh_sec"];
                    bool tioOn = (active == "tomorrow");
                    bool wapOn = (active == "weatherapi");
                    // Importante: aplicar primero el que se va a apagar para
                    // que la exclusion mutua del setter contrario no lo
                    // sobreescriba. setX(false,...) no toca el otro flag.
                    if (tioOn) {
                        Config::setWeatherApiSettings(false, wapKey, wapRef);
                        Config::setTomorrowSettings(true, tioKey, tioRef);
                    } else if (wapOn) {
                        Config::setTomorrowSettings(false, tioKey, tioRef);
                        Config::setWeatherApiSettings(true, wapKey, wapRef);
                    } else {
                        Config::setTomorrowSettings(false, tioKey, tioRef);
                        Config::setWeatherApiSettings(false, wapKey, wapRef);
                    }
                    JsonDocument res;
                    res["ok"] = true;
                    res["active"] = active;
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
    // POST /api/button?b=left|center|right: simula pulsacion fisica del TTP
    // correspondiente (mismo flujo: ripple visual + cambio de modo o brillo).
    server.on("/api/button", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            int idx = -1;
            if (req->hasParam("b")) {
                String v = req->getParam("b")->value();
                if (v == "left")        idx = 0;
                else if (v == "center") idx = 1;
                else if (v == "right")  idx = 2;
                else if (v == "0" || v == "1" || v == "2") idx = v.toInt();
            }
            if (idx < 0) {
                req->send(400, "application/json",
                          "{\"error\":\"need b=left|center|right\"}");
                return;
            }
            requestButtonPress(idx);
            req->send(200, "application/json", "{\"ok\":true}");
        });

    // POST /api/autoupdate/check: dispara un check de auto-update ad-hoc.
    // Respuesta inmediata; el chequeo + flash lo hace el loop principal en
    // la siguiente iteracion (bloquea hasta varios minutos si hay release
    // nueva, durante ese tiempo el reloj se cambia a splash de update).
    server.on("/api/autoupdate/check", HTTP_POST, [](AsyncWebServerRequest* req) {
        AutoUpdate::requestCheck();
        req->send(200, "application/json", "{\"ok\":true}");
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
