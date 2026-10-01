#include "OpenAIStats.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <memory>
#include <time.h>

#include "Config.h"

namespace OpenAIStats {

Usage usage;

static const char* AUTH_BASE    = "https://auth.openai.com";
static const char* USAGE_URL    = "https://chatgpt.com/backend-api/wham/usage";
static const char* CODEX_BASE   = "https://chatgpt.com/backend-api/codex";
// Modelo de reserva para el "hola" si /codex/models falla. Es el que ofrecia
// la cuenta Plus al probarlo (2026-10).
static const char* HOLA_FALLBACK_MODEL = "codex-auto-review";
// client_id publico de Codex CLI: es el que tiene habilitado el login por
// codigo de dispositivo.
static const char* CLIENT_ID    = "app_EMoamEEZ73f0CkXaXp7hrann";
static const char* USER_AGENT   = "Pixelario/1.0 (ESP32-S3)";
static const char* CREDS_PATH   = "/openai.json";
static const char* CREDS_TMP    = "/openai.json.tmp";
static const char* CACHE_PATH   = "/openaicache.json";
static constexpr time_t TIME_VALID = 1672531200;      // 2023-01-01
// El access token dura 10 dias. Se renueva solo cuando le quedan menos de 6h
// (o si wham/usage contesta 401): cada renovacion rota el refresh token, y
// si la respuesta se pierde por la WiFi el refresh viejo ya no vale.
static constexpr time_t REFRESH_MARGIN_S = 6 * 3600;
static constexpr uint32_t LOGIN_TIMEOUT_MS = 15UL * 60UL * 1000UL;

struct Creds {
    String refreshToken;
    String accessToken;
    time_t accessExp = 0;
    // OpenAI indica desde cuando se puede renovar (~9 dias tras emitir).
    // Antes de eso un 401 suelto no justifica rotar el refresh token.
    time_t earliestRefresh = 0;
    String accountId;
};

static Creds             s_creds;
static Status            s_status;
static String            s_lastRaw;
static SemaphoreHandle_t s_mtx = nullptr;
static TaskHandle_t      s_task = nullptr;
static volatile bool     s_loginRequested = false;
static volatile bool     s_cancelLogin = false;
static volatile bool     s_holaRequested = false;
static volatile ClaudeStats::HolaStatus s_holaState = ClaudeStats::HolaStatus::NONE;

struct Lock {
    Lock()  { if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY); }
    ~Lock() { if (s_mtx) xSemaphoreGive(s_mtx); }
};

Status status() {
    Lock l;
    Status st = s_status;
    st.hola = s_holaState;
    return st;
}
bool holaPending() { return s_holaState == ClaudeStats::HolaStatus::PENDING; }
String lastRawUsage() { Lock l; return s_lastRaw; }

bool isConfigured() {
    Lock l;
    return s_creds.refreshToken.length() > 0;
}

static void setError(const String& e) {
    Lock l;
    s_status.error = e;
}

static bool fsReady() {
    return LittleFS.begin(true, "/littlefs", 10, "littlefs");
}

// ── Credenciales en LittleFS (fuera de cfg.json: nunca en backups) ──────

static void loadCreds() {
    if (!fsReady() || !LittleFS.exists(CREDS_PATH)) return;
    File f = LittleFS.open(CREDS_PATH, "r");
    if (!f) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;
    Lock l;
    s_creds.refreshToken = doc["refresh_token"] | "";
    s_creds.accessToken  = doc["access_token"] | "";
    s_creds.accessExp    = (time_t)(doc["access_exp"] | 0);
    s_creds.earliestRefresh = (time_t)(doc["earliest_refresh_at"] | 0);
    s_creds.accountId    = doc["account_id"] | "";
    s_status.email       = doc["email"] | "";
    s_status.plan        = doc["plan"] | "";
    s_status.login = s_creds.refreshToken.length() ? Login::CONNECTED : Login::NONE;
}

// tmp + rename: si se corta la corriente a mitad, queda el fichero anterior
// entero y no un refresh token a medias.
static bool saveCreds() {
    if (!fsReady()) return false;
    JsonDocument doc;
    {
        Lock l;
        doc["refresh_token"] = s_creds.refreshToken;
        doc["access_token"]  = s_creds.accessToken;
        doc["access_exp"]    = (uint32_t)s_creds.accessExp;
        doc["earliest_refresh_at"] = (uint32_t)s_creds.earliestRefresh;
        doc["account_id"]    = s_creds.accountId;
        doc["email"]         = s_status.email;
        doc["plan"]          = s_status.plan;
    }
    File f = LittleFS.open(CREDS_TMP, "w");
    if (!f) return false;
    size_t n = serializeJson(doc, f);
    f.close();
    if (n == 0) return false;
    LittleFS.remove(CREDS_PATH);
    return LittleFS.rename(CREDS_TMP, CREDS_PATH);
}

static void loadCache() {
    if (!fsReady() || !LittleFS.exists(CACHE_PATH)) return;
    File f = LittleFS.open(CACHE_PATH, "r");
    if (!f) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;
    auto load = [](JsonVariantConst v, ClaudeStats::UsageWindow& w, long& win) {
        if (v.isNull()) return;
        w.valid       = v["valid"] | false;
        w.utilization = v["used"] | 0.0;
        w.resetsAt    = (time_t)(v["resets_at"] | 0);
        win           = v["window_s"] | win;
    };
    load(doc["five_hour"], usage.fiveHour, usage.fiveWindowSec);
    load(doc["weekly"], usage.weekly, usage.weeklyWindowSec);
    usage.hasData = usage.fiveHour.valid || usage.weekly.valid;
}

static void saveCache() {
    if (!fsReady()) return;
    JsonDocument doc;
    auto save = [](JsonObject o, const ClaudeStats::UsageWindow& w, long win) {
        o["valid"]     = w.valid;
        o["used"]      = w.utilization;
        o["resets_at"] = (uint32_t)w.resetsAt;
        o["window_s"]  = win;
    };
    save(doc["five_hour"].to<JsonObject>(), usage.fiveHour, usage.fiveWindowSec);
    save(doc["weekly"].to<JsonObject>(), usage.weekly, usage.weeklyWindowSec);
    File f = LittleFS.open(CACHE_PATH, "w");
    if (!f) return;
    serializeJson(doc, f);
    f.close();
}

// ── HTTP ─────────────────────────────────────────────────────────────────

static int httpCall(bool post, const String& url, const String& body, const char* contentType,
                    const String& bearer, const String& accountId, String& out) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(15000);
    if (!http.begin(client, url)) return -1;
    http.setUserAgent(USER_AGENT);
    http.addHeader("Accept", "application/json");
    if (contentType) http.addHeader("Content-Type", contentType);
    if (bearer.length()) http.addHeader("Authorization", "Bearer " + bearer);
    if (accountId.length()) http.addHeader("ChatGPT-Account-Id", accountId);
    int code = post ? http.POST(body) : http.GET();
    out = (code > 0) ? http.getString() : String();
    http.end();
    return code;
}

static String urlEncode(const String& s) {
    static const char* HEX_CHARS = "0123456789ABCDEF";
    String o;
    o.reserve(s.length() + 16);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            o += c;
        } else {
            o += '%';
            o += HEX_CHARS[(c >> 4) & 0xF];
            o += HEX_CHARS[c & 0xF];
        }
    }
    return o;
}

// Payload de un JWT (base64url sin padding) parseado como JSON. Sin
// verificar la firma: solo se leen claims de un token recien recibido por TLS.
static bool jwtPayload(const String& jwt, JsonDocument& doc) {
    int a = jwt.indexOf('.');
    int b = (a >= 0) ? jwt.indexOf('.', a + 1) : -1;
    if (a < 0 || b < 0) return false;
    String p = jwt.substring(a + 1, b);
    p.replace('-', '+');
    p.replace('_', '/');
    while (p.length() % 4) p += '=';
    std::unique_ptr<uint8_t[]> buf(new uint8_t[p.length()]);
    size_t olen = 0;
    if (mbedtls_base64_decode(buf.get(), p.length(), &olen,
                              (const uint8_t*)p.c_str(), p.length()) != 0) return false;
    return deserializeJson(doc, (const char*)buf.get(), olen) == DeserializationError::Ok;
}

// Aplica una respuesta de /oauth/token (login o refresh) y la guarda. El
// refresh token nuevo se persiste antes de usar el access token nuevo.
static bool applyTokens(const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return false;
    String access  = doc["access_token"] | "";
    String refresh = doc["refresh_token"] | "";
    String idTok   = doc["id_token"] | "";
    long expiresIn = doc["expires_in"] | 0L;
    time_t earliest = (time_t)(doc["earliest_refresh_at"] | 0L);
    if (!access.length()) return false;
    time_t now = time(nullptr);
    time_t exp = 0;
    {
        JsonDocument claims;
        if (jwtPayload(access, claims)) exp = (time_t)(claims["exp"] | 0L);
    }
    if (!exp && now > TIME_VALID && expiresIn > 0) exp = now + expiresIn;
    String accountId, email, plan;
    if (idTok.length()) {
        JsonDocument claims;
        if (jwtPayload(idTok, claims)) {
            JsonVariantConst auth = claims["https://api.openai.com/auth"];
            accountId = auth["chatgpt_account_id"] | "";
            plan      = auth["chatgpt_plan_type"] | "";
            email     = claims["email"] | "";
        }
    }
    {
        Lock l;
        if (refresh.length()) s_creds.refreshToken = refresh;
        s_creds.accessToken = access;
        s_creds.accessExp = exp;
        s_creds.earliestRefresh = earliest;
        if (accountId.length()) s_creds.accountId = accountId;
        if (email.length()) s_status.email = email;
        if (plan.length()) s_status.plan = plan;
    }
    if (!saveCreds()) Serial.println("[openai] no se pudieron guardar las credenciales");
    return true;
}

enum class RefreshResult : uint8_t { OK, TRANSIENT, REJECTED };

static void dropSession(const String& why) {
    {
        Lock l;
        s_creds = Creds();
        s_status.login = Login::ERROR;
        s_status.error = why;
    }
    if (fsReady()) LittleFS.remove(CREDS_PATH);
}

static RefreshResult refreshTokens() {
    String refresh;
    { Lock l; refresh = s_creds.refreshToken; }
    if (!refresh.length()) return RefreshResult::REJECTED;
    JsonDocument req;
    req["grant_type"]    = "refresh_token";
    req["client_id"]     = CLIENT_ID;
    req["refresh_token"] = refresh;
    String body, out;
    serializeJson(req, body);
    int code = httpCall(true, String(AUTH_BASE) + "/oauth/token", body, "application/json", "", "", out);
    if (code == 200 && applyTokens(out)) {
        Serial.println("[openai] token renovado");
        return RefreshResult::OK;
    }
    if (code == 400 || code == 401) {
        Serial.printf("[openai] refresh rechazado (%d): %s\n", code, out.substring(0, 200).c_str());
        dropSession("La sesión ha caducado o se ha cerrado. Vuelve a conectar.");
        return RefreshResult::REJECTED;
    }
    setError(String("No se pudo renovar la sesión (HTTP ") + code + ")");
    return RefreshResult::TRANSIENT;
}

// ── Login por codigo de dispositivo ──────────────────────────────────────

static void runLogin() {
    s_cancelLogin = false;
    {
        Lock l;
        s_status.login = Login::WAITING_USER;
        s_status.userCode = "";
        s_status.error = "";
    }
    JsonDocument req;
    req["client_id"] = CLIENT_ID;
    String body, out;
    serializeJson(req, body);
    int code = httpCall(true, String(AUTH_BASE) + "/api/accounts/deviceauth/usercode",
                        body, "application/json", "", "", out);
    JsonDocument resp;
    if (code != 200 || deserializeJson(resp, out) != DeserializationError::Ok) {
        Lock l;
        s_status.login = Login::ERROR;
        s_status.error = String("No se pudo pedir el código (HTTP ") + code + ")";
        return;
    }
    String deviceAuthId = resp["device_auth_id"] | "";
    String userCode = resp["user_code"] | "";
    if (!userCode.length()) userCode = resp["usercode"] | "";
    // "interval" llega como string ("5"); se acepta tambien como numero.
    long interval = resp["interval"].is<const char*>() ? atol(resp["interval"].as<const char*>())
                                                        : (long)(resp["interval"] | 5L);
    if (interval < 2) interval = 5;
    time_t now = time(nullptr);
    {
        Lock l;
        s_status.userCode = userCode;
        s_status.codeExpiresAt = (now > TIME_VALID) ? now + (time_t)(LOGIN_TIMEOUT_MS / 1000) : 0;
    }
    Serial.printf("[openai] codigo de dispositivo: %s\n", userCode.c_str());

    JsonDocument poll;
    poll["device_auth_id"] = deviceAuthId;
    poll["user_code"] = userCode;
    String pollBody;
    serializeJson(poll, pollBody);
    uint32_t start = millis();
    JsonDocument grant;
    for (;;) {
        // Esperar con notify para que "desconectar" corte la espera.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(interval * 1000));
        if (s_cancelLogin) {
            Lock l;
            s_status.login = Login::NONE;
            s_status.userCode = "";
            return;
        }
        if (millis() - start > LOGIN_TIMEOUT_MS) {
            Lock l;
            s_status.login = Login::ERROR;
            s_status.userCode = "";
            s_status.error = "El código ha caducado sin confirmarse.";
            return;
        }
        code = httpCall(true, String(AUTH_BASE) + "/api/accounts/deviceauth/token",
                        pollBody, "application/json", "", "", out);
        if (code == 403 || code == 404 || code <= 0) continue;   // pendiente o red
        if (code == 200 && deserializeJson(grant, out) == DeserializationError::Ok) break;
        Lock l;
        s_status.login = Login::ERROR;
        s_status.userCode = "";
        s_status.error = String("El login ha fallado (HTTP ") + code + ")";
        return;
    }

    String form = String("grant_type=authorization_code&client_id=") + CLIENT_ID +
                  "&code=" + urlEncode(grant["authorization_code"] | "") +
                  "&redirect_uri=" + urlEncode(String(AUTH_BASE) + "/deviceauth/callback") +
                  "&code_verifier=" + urlEncode(grant["code_verifier"] | "");
    code = httpCall(true, String(AUTH_BASE) + "/oauth/token", form,
                    "application/x-www-form-urlencoded", "", "", out);
    if (code != 200 || !applyTokens(out)) {
        Lock l;
        s_status.login = Login::ERROR;
        s_status.userCode = "";
        s_status.error = String("No se pudo completar el login (HTTP ") + code + ")";
        return;
    }
    Lock l;
    s_status.login = Login::CONNECTED;
    s_status.userCode = "";
    s_status.error = "";
    Serial.printf("[openai] conectado (%s)\n", s_status.plan.c_str());
}

// ── Uso ──────────────────────────────────────────────────────────────────

static void parseWindow(JsonVariantConst v, ClaudeStats::UsageWindow& w, long& winSec, time_t now) {
    w = ClaudeStats::UsageWindow();
    if (v.isNull() || !(v["used_percent"].is<double>() || v["used_percent"].is<long>())) return;
    double used = v["used_percent"].as<double>();
    if (used < 0) used = 0;
    if (used > 100) used = 100;
    w.valid = true;
    w.utilization = used;
    w.resetsAt = (time_t)(v["reset_at"] | 0L);
    if (!w.resetsAt && now > TIME_VALID && v["reset_after_seconds"].is<long>())
        w.resetsAt = now + (time_t)v["reset_after_seconds"].as<long>();
    long win = v["limit_window_seconds"] | 0L;
    if (win > 0) winSec = win;
}

static bool fetchUsageOnce() {
    time_t now = time(nullptr);
    bool needRefresh;
    {
        Lock l;
        needRefresh = !s_creds.accessToken.length() ||
                      (now > TIME_VALID && s_creds.accessExp && now > s_creds.accessExp - REFRESH_MARGIN_S);
    }
    if (needRefresh && refreshTokens() != RefreshResult::OK) return false;

    for (int attempt = 0; attempt < 2; attempt++) {
        String access, account, out;
        { Lock l; access = s_creds.accessToken; account = s_creds.accountId; }
        int code = httpCall(false, USAGE_URL, "", nullptr, access, account, out);
        if (code == 401 && attempt == 0) {
            // Token revocado o caducado antes de tiempo: un solo refresh, y
            // solo si OpenAI ya permite renovar; si no, se reintenta en el
            // siguiente ciclo sin rotar la sesion.
            time_t earliest;
            { Lock l; earliest = s_creds.earliestRefresh; }
            if (now > TIME_VALID && earliest && now < earliest) {
                setError("wham/usage HTTP 401; se reintentará");
                return false;
            }
            if (refreshTokens() != RefreshResult::OK) return false;
            continue;
        }
        { Lock l; s_lastRaw = out.substring(0, 4096); }
        if (code != 200) {
            setError(String("wham/usage HTTP ") + code);
            return false;
        }
        JsonDocument doc;
        if (deserializeJson(doc, out) != DeserializationError::Ok) {
            setError("Respuesta de uso no válida");
            return false;
        }
        JsonVariantConst rl = doc["rate_limit"];
        ClaudeStats::UsageWindow five, week;
        long fiveWin = usage.fiveWindowSec, weekWin = usage.weeklyWindowSec;
        parseWindow(rl["primary_window"], five, fiveWin, now);
        parseWindow(rl["secondary_window"], week, weekWin, now);
        usage.fiveHour = five;
        usage.weekly = week;
        usage.fiveWindowSec = fiveWin;
        usage.weeklyWindowSec = weekWin;
        usage.hasData = five.valid || week.valid;
        Lock l;
        String plan = doc["plan_type"] | "";
        if (plan.length()) s_status.plan = plan;
        s_status.error = "";
        s_status.lastOkAtMs = millis();
        return true;
    }
    return false;
}

// ── "hola": abre la ventana de 5h ────────────────────────────────────────

// Primer modelo que ofrece la cuenta en /codex/models. La respuesta trae
// metadatos de cada modelo (puede ser grande): se filtra al parsear.
static String pickHolaModel(const String& access, const String& account) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(15000);
    String model = HOLA_FALLBACK_MODEL;
    if (!http.begin(client, String(CODEX_BASE) + "/models?client_version=0.99.0")) return model;
    http.setUserAgent(USER_AGENT);
    http.addHeader("Accept", "application/json");
    http.addHeader("Authorization", "Bearer " + access);
    if (account.length()) http.addHeader("ChatGPT-Account-Id", account);
    if (http.GET() == 200) {
        JsonDocument filter;
        filter["models"][0]["slug"] = true;
        JsonDocument doc;
        if (deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter)) ==
            DeserializationError::Ok) {
            const char* slug = doc["models"][0]["slug"] | (const char*)nullptr;
            if (slug && *slug) model = slug;
        }
    }
    http.end();
    return model;
}

// Un mensaje de una palabra a Codex: basta para que empiece a contar la
// ventana de 5h (comprobado: reset_after_seconds pasa de 18000 fijo a bajar).
static bool openWindowOnce(String& err) {
    time_t now = time(nullptr);
    bool needRefresh;
    {
        Lock l;
        needRefresh = !s_creds.accessToken.length() ||
                      (now > TIME_VALID && s_creds.accessExp && now > s_creds.accessExp - REFRESH_MARGIN_S);
    }
    if (needRefresh && refreshTokens() != RefreshResult::OK) { err = "sin sesión válida"; return false; }
    for (int attempt = 0; attempt < 2; attempt++) {
        String access, account;
        { Lock l; access = s_creds.accessToken; account = s_creds.accountId; }
        String model = pickHolaModel(access, account);
        JsonDocument req;
        req["model"] = model;
        req["stream"] = true;
        req["instructions"] = "Responde con una sola palabra.";
        JsonObject msg = req["input"].to<JsonArray>().add<JsonObject>();
        msg["type"] = "message";
        msg["role"] = "user";
        JsonObject part = msg["content"].to<JsonArray>().add<JsonObject>();
        part["type"] = "input_text";
        part["text"] = "hola";
        req["tool_choice"] = "auto";
        req["parallel_tool_calls"] = false;
        req["store"] = false;
        req["include"].to<JsonArray>();
        String body, out;
        serializeJson(req, body);

        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.setTimeout(60000);
        if (!http.begin(client, String(CODEX_BASE) + "/responses")) { err = "sin conexión"; return false; }
        http.setUserAgent(USER_AGENT);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Accept", "text/event-stream");
        http.addHeader("Authorization", "Bearer " + access);
        if (account.length()) http.addHeader("ChatGPT-Account-Id", account);
        int code = http.POST(body);
        out = (code > 0) ? http.getString() : String();
        http.end();
        if (code == 401 && attempt == 0) {
            if (refreshTokens() != RefreshResult::OK) { err = "sesión caducada"; return false; }
            continue;
        }
        if (code == 200 && out.indexOf("response.completed") >= 0) {
            Serial.printf("[openai] hola ok (%s)\n", model.c_str());
            return true;
        }
        err = String("HTTP ") + code + (out.length() ? ": " + out.substring(0, 120) : String());
        return false;
    }
    err = "sesión caducada";
    return false;
}

// ── Task ─────────────────────────────────────────────────────────────────

static void taskBody(void*) {
    vTaskDelay(pdMS_TO_TICKS(9000));   // no competir con el resto de fetches del boot
    for (;;) {
        // while: pedir otro codigo mientras se espera el anterior cancela ese
        // login y deja este pendiente; hay que atenderlo ya, no tras el refresco.
        while (s_loginRequested) {
            s_loginRequested = false;
            runLogin();
        }
        if (s_holaRequested) {
            s_holaRequested = false;
            if (isConfigured() && WiFi.isConnected()) {
                s_holaState = ClaudeStats::HolaStatus::PENDING;
                String err;
                bool ok = openWindowOnce(err);
                s_holaState = ok ? ClaudeStats::HolaStatus::OK : ClaudeStats::HolaStatus::FAIL;
                Lock l;
                s_status.holaError = ok ? String() : err;
            } else {
                s_holaState = ClaudeStats::HolaStatus::FAIL;
                Lock l;
                s_status.holaError = "sin cuenta conectada";
            }
        }
        if (isConfigured() && WiFi.isConnected()) {
            if (fetchUsageOnce()) {
                Serial.printf("[openai] ok: 5h=%.0f%% 7d=%.0f%%\n",
                              usage.fiveHour.valid ? usage.fiveHour.utilization : -1,
                              usage.weekly.valid ? usage.weekly.utilization : -1);
                saveCache();
            } else {
                Serial.printf("[openai] fetch fail: %s\n", status().error.c_str());
            }
        }
        uint32_t waitMs = (uint32_t)Config::cfg.openaiRefreshSec * 1000UL;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
    }
}

void begin() {
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
    loadCreds();
    if (isConfigured()) loadCache();
}

void taskStart() {
    if (s_task) return;
    xTaskCreatePinnedToCore(taskBody, "openai", 8192, nullptr, 1, &s_task, 1);
}

void requestRefresh() {
    if (s_task) xTaskNotifyGive(s_task);
}

void requestOpenWindow() {
    s_holaRequested = true;
    if (s_task) xTaskNotifyGive(s_task);
}

void requestLogin() {
    s_cancelLogin = true;     // si ya habia un login esperando, se reinicia
    s_loginRequested = true;
    if (s_task) xTaskNotifyGive(s_task);
}

void logout() {
    s_cancelLogin = true;
    s_loginRequested = false;
    {
        Lock l;
        s_creds = Creds();
        s_status = Status();
        s_lastRaw = "";
    }
    usage = Usage();
    if (fsReady()) {
        LittleFS.remove(CREDS_PATH);
        LittleFS.remove(CACHE_PATH);
    }
    if (s_task) xTaskNotifyGive(s_task);
}

}  // namespace OpenAIStats
