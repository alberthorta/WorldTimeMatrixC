#include <Arduino.h>
#include <ArduinoOTA.h>
#include <time.h>

#include "Config.h"
#include "Display.h"
#include "Icons.h"
#include "WebApi.h"
#include "Weather.h"
#include "WifiSetup.h"

volatile bool g_pendingReset = false;

static constexpr time_t TIME_VALID_THRESHOLD = 1672531200;   // 2023-01-01

static bool inNightWindow(uint16_t nowMins) {
    const auto& nm = Config::cfg.nightMode;
    if (!nm.enabled) return false;
    if (nm.startMins == nm.endMins) return false;
    if (nm.startMins < nm.endMins) {
        return nowMins >= nm.startMins && nowMins < nm.endMins;
    }
    return nowMins >= nm.startMins || nowMins < nm.endMins;
}

static float effectiveBrightness(time_t utc, int referenceOffsetSec) {
    const auto& nm = Config::cfg.nightMode;
    if (nm.enabled && utc > TIME_VALID_THRESHOLD) {
        time_t local = utc + referenceOffsetSec;
        struct tm tm;
        gmtime_r(&local, &tm);
        uint16_t nowMins = tm.tm_hour * 60 + tm.tm_min;
        if (inNightWindow(nowMins)) return nm.brightness;
    }
    return Config::cfg.brightness;
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("[boot] WorldTime fw starting (build OTA test)");

    Icons::begin();          // Defaults (nombres + frame inicial); Config los sobrescribe.
    Config::begin();
    Weather::loadCache();   // muestra ultima meteo conocida mientras NTP/fetch arrancan
    Display::begin();
    Display::setBrightness((uint8_t)(Config::cfg.brightness * 255));
    WifiSetup::begin();
    if (WifiSetup::currentMode() == WifiSetup::Mode::Sta) {
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        Serial.println("[time] NTP requested");
        ArduinoOTA.setHostname("worldtime");
        ArduinoOTA.setPassword("matrix");
        ArduinoOTA.onStart([]() { Serial.println("[ota] start"); });
        ArduinoOTA.onEnd([]() { Serial.println("\n[ota] end"); });
        ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
            Serial.printf("[ota] %u%%\r", (p * 100) / t);
        });
        ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[ota] err %u\n", e); });
        ArduinoOTA.begin();
        Serial.println("[ota] ArduinoOTA up (host=worldtime auth=matrix)");
    }
    WebApi::begin();
    Weather::taskStart();
}

void loop() {
    ArduinoOTA.handle();
    if (g_pendingReset) {
        delay(500);
        ESP.restart();
    }

    static uint32_t lastRender = 0;
    static uint8_t lastBrightness = 255;
    // Renderizamos a 10fps (100ms) para que las animaciones de iconos puedan
    // tickar con granularidad fina. Coste: bajo, dado que el panel se refresca
    // por DMA en hardware.
    if (millis() - lastRender < 100) {
        delay(5);
        return;
    }
    lastRender = millis();

    time_t utc = time(nullptr);
    bool timeOk = utc > TIME_VALID_THRESHOLD;

    // Brillo: usa la primera ciudad como timezone de referencia para la ventana
    // de modo noche (igual que la version Python).
    int refOffset = Weather::data[0].hasData ? Weather::data[0].offsetSec : 0;
    float effBright = effectiveBrightness(utc, refOffset);
    uint8_t targetBright = (uint8_t)(effBright * 255);
    if (targetBright != lastBrightness) {
        Display::setBrightness(targetBright);
        lastBrightness = targetBright;
    }

    Display::Row rows[4];
    bool showColon = true;
    if (Config::cfg.colonBlink && timeOk) {
        showColon = (utc % 2) == 0;
    }

    // Buffers persistentes para nombres dinamicos ($DATE -> "DD/MM").
    // Statics de funcion: validos durante toda la vida del programa, suficientes
    // mientras renderRows lea r.name sincronamente en esta misma iteracion.
    static char nameBuffers[4][8];

    for (int i = 0; i < 4; i++) {
        const Config::City& cc = Config::cfg.cities[i];
        const Weather::Data& d = Weather::data[i];
        Display::Row& r = rows[i];

        if (cc.name == "$DATE" && timeOk) {
            time_t local = utc + d.offsetSec;
            struct tm tm;
            gmtime_r(&local, &tm);
            snprintf(nameBuffers[i], sizeof(nameBuffers[i]),
                     "%02d/%02d", tm.tm_mday, tm.tm_year % 100);
            r.name = nameBuffers[i];
        } else {
            r.name = cc.name.c_str();
        }
        r.color = Display::rgb888to565(cc.colorRgb);
        r.hasTime = timeOk;
        if (timeOk) {
            time_t local = utc + d.offsetSec;
            struct tm tm;
            gmtime_r(&local, &tm);
            r.hour = tm.tm_hour;
            r.minute = tm.tm_min;
        } else {
            r.hour = 0; r.minute = 0;
        }
        r.hasWeather = d.hasData;
        r.tempC = d.tempC;
        r.icon = d.hasData
                     ? Weather::iconForCode(d.code, d.isDay)
                     : Display::IconType::NONE;
        r.showColon = showColon;
    }
    Display::renderRows(rows);
}
