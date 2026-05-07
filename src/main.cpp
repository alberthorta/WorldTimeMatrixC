#include <Arduino.h>
#include <ArduinoOTA.h>
#include <math.h>
#include <sys/time.h>
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
    // Renderizamos a ~30fps (33ms) para que la barra de segundos sub-pixel
    // se vea fluida. Coste: bajo — panel via DMA, render es solo composicion
    // de framebuffer logico.
    if (millis() - lastRender < 33) {
        delay(2);
        return;
    }
    lastRender = millis();

    time_t utc = time(nullptr);
    bool timeOk = utc > TIME_VALID_THRESHOLD;
    // Tiempo con resolucion sub-segundo para la barra continua (gettimeofday
    // sale del reloj NTP-sincronizado, mismo que time(nullptr)).
    float secondOfMinuteF = -1.0f;
    if (timeOk) {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        double secsF = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
        secondOfMinuteF = (float)fmod(secsF, 60.0);
    }

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
    // Fade-in/out del ":" durante los primeros ~4 frames de cada segundo,
    // suavizando la transicion on/off del parpadeo. Sin colonBlink esta a 1.
    float colonAlpha = 1.0f;
    if (Config::cfg.colonBlink && timeOk) {
        int intSec = (int)floorf(secondOfMinuteF);
        float frac = secondOfMinuteF - (float)intSec;
        float target = ((intSec % 2) == 0) ? 1.0f : 0.0f;
        float prev   = 1.0f - target;
        const float FADE_SEC = 4.0f / 30.0f;   // 4 frames a 30fps ≈ 133ms
        if (frac < FADE_SEC) {
            float p = frac / FADE_SEC;
            colonAlpha = prev * (1.0f - p) + target * p;
        } else {
            colonAlpha = target;
        }
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
                     "%02d/%02d", tm.tm_mday, tm.tm_mon + 1);
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
        r.colonAlpha = colonAlpha;
        r.omIndicator = Config::cfg.omIndicator && d.hasData && d.tempSource == 1;
    }
    Display::renderRows(rows, secondOfMinuteF);
}
