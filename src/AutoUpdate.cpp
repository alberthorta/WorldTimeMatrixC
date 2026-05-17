#include "AutoUpdate.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>

namespace AutoUpdate {

// Endpoint fijo del repo. Si en el futuro se cambia el nombre/usuario,
// actualizar aqui.
static constexpr const char* GITHUB_API =
    "https://api.github.com/repos/alberthorta/WorldTimeMatrixC/releases/latest";

ReleaseInfo fetchLatestRelease() {
    ReleaseInfo r;
    WiFiClientSecure client;
    // Sin pinear CA. GitHub rota certs y mantener un bundle aqui es fragil;
    // como contramedida principal contamos con que el flashing solo flippea
    // particion tras CRC OK de Update — un MITM no podria meter un binario
    // valido sin firmar.
    client.setInsecure();

    HTTPClient http;
    http.setUserAgent("WorldTimeMatrixC");
    http.setTimeout(15000);
    http.useHTTP10(true);   // mejor para ArduinoJson stream
    if (!http.begin(client, GITHUB_API)) {
        Serial.println("[autoupd] http.begin failed");
        return r;
    }

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[autoupd] api status %d\n", code);
        http.end();
        return r;
    }

    // Filter para no tragarse los 50KB de JSON de una release. Solo extraemos
    // tag_name y assets[].{name,browser_download_url}.
    JsonDocument filter;
    filter["tag_name"] = true;
    JsonObject assetFilter = filter["assets"][0].to<JsonObject>();
    assetFilter["name"] = true;
    assetFilter["browser_download_url"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        Serial.printf("[autoupd] json err: %s\n", err.c_str());
        return r;
    }

    const char* tag = doc["tag_name"] | "";
    if (!tag || !tag[0]) {
        Serial.println("[autoupd] no tag_name");
        return r;
    }

    JsonArray assets = doc["assets"];
    for (JsonObject a : assets) {
        const char* name = a["name"] | "";
        String n(name);
        if (n.endsWith(".bin")) {
            const char* url = a["browser_download_url"] | "";
            if (url && url[0]) {
                r.binUrl = url;
                break;
            }
        }
    }
    if (r.binUrl.isEmpty()) {
        Serial.println("[autoupd] no .bin asset");
        return r;
    }

    r.tagName = tag;
    r.found = true;
    Serial.printf("[autoupd] latest=%s url=%s\n", r.tagName.c_str(), r.binUrl.c_str());
    return r;
}

bool downloadAndFlash(const String& url, void (*progress)(int)) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setUserAgent("WorldTimeMatrixC");
    http.setTimeout(30000);
    // github.com -> objects.githubusercontent.com requiere seguir redirects.
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, url)) {
        Serial.println("[autoupd] dl http.begin failed");
        return false;
    }

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[autoupd] dl status %d\n", code);
        http.end();
        return false;
    }

    int total = http.getSize();
    if (total <= 0) {
        Serial.printf("[autoupd] bad size %d\n", total);
        http.end();
        return false;
    }
    Serial.printf("[autoupd] downloading %d bytes\n", total);

    if (!Update.begin((size_t)total)) {
        Serial.println("[autoupd] Update.begin failed");
        Update.printError(Serial);
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    size_t written = 0;
    int lastPct = -1;
    uint32_t lastDataMs = millis();
    while (written < (size_t)total) {
        // Si la conexion se queda muda > 20s, abort.
        if (millis() - lastDataMs > 20000) {
            Serial.println("[autoupd] stream stalled");
            break;
        }
        size_t avail = stream->available();
        if (!avail) {
            // En espera de mas datos; sin spin tight.
            delay(2);
            continue;
        }
        size_t toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
        int n = stream->readBytes(buf, toRead);
        if (n <= 0) {
            delay(2);
            continue;
        }
        if (Update.write(buf, n) != (size_t)n) {
            Serial.println("[autoupd] Update.write failed");
            Update.printError(Serial);
            Update.abort();
            http.end();
            return false;
        }
        written += n;
        lastDataMs = millis();
        if (progress) {
            int pct = (int)((written * 100ULL) / (size_t)total);
            if (pct != lastPct) {
                progress(pct);
                lastPct = pct;
            }
        }
    }
    http.end();

    if (written != (size_t)total) {
        Serial.printf("[autoupd] incomplete %u/%d\n", (unsigned)written, total);
        Update.abort();
        return false;
    }

    if (!Update.end(true)) {
        Serial.println("[autoupd] Update.end failed");
        Update.printError(Serial);
        return false;
    }

    Serial.println("[autoupd] flash OK");
    return true;
}

// Flag de "check on demand" disparado desde WebApi y consumido por el loop
// principal cuando esta listo para hacer el fetch/flash. Volatil porque WebApi
// vive en otra task (AsyncWebServer).
static volatile bool s_checkRequested = false;
void requestCheck() { s_checkRequested = true; }
bool consumeCheckRequest() {
    bool req = s_checkRequested;
    s_checkRequested = false;
    return req;
}

}  // namespace AutoUpdate
