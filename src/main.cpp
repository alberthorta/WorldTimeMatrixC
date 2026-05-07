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

    pinMode(PIN_BUTTON_UP, INPUT_PULLUP);
    Icons::begin();          // Defaults (nombres + frame inicial); Config los sobrescribe.
    Config::begin();
    Weather::loadCache();   // muestra ultima meteo conocida mientras NTP/fetch arrancan
    Display::begin();
    Display::setBrightness((uint8_t)(Config::cfg.brightness * 255));
    {
        const char* lines[] = {"WorldTime", "Assigning IP", "", ""};
        Display::drawSplash(lines, 2);
    }
    WifiSetup::begin();
    if (WifiSetup::currentMode() == WifiSetup::Mode::Sta) {
        // Mensaje "IP : x.x.x.x" durante 5s antes de empezar el reloj.
        char ipBuf[20];
        snprintf(ipBuf, sizeof(ipBuf), "%s", WifiSetup::currentIp().c_str());
        const char* lines[] = {"WorldTime", "IP :", ipBuf, ""};
        Display::drawSplash(lines, 3);
        delay(5000);
    } else {
        // Modo AP: deja el splash con instrucciones para el usuario. El loop
        // detectara el modo y no pintara el reloj.
        const char* lines[] = {"WorldTime", "Connect to", "WorldTime-Setup", "to configure"};
        Display::drawSplash(lines, 4);
    }
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
    WifiSetup::tickHealth();
    // Botón UP mantenido 3s → forzar modo AP. Util para reconfigurar WiFi
    // sin tener que esperar a que falle STA. Edge-detect: pressedSinceMs
    // arranca al detectar la primera lectura LOW; si suelta antes de 3s
    // resetea. INPUT_PULLUP, asi que LOW = pulsado.
    static uint32_t btnUpPressedSinceMs = 0;
    if (WifiSetup::currentMode() == WifiSetup::Mode::Sta) {
        bool pressed = (digitalRead(PIN_BUTTON_UP) == LOW);
        if (pressed) {
            if (btnUpPressedSinceMs == 0) btnUpPressedSinceMs = millis();
            else if (millis() - btnUpPressedSinceMs >= 3000) {
                Serial.println("[btn] UP held 3s → switching to AP");
                WifiSetup::switchToAp();
                const char* lines[] = {"WorldTime", "Connect to", "WorldTime-Setup", "to configure"};
                Display::drawSplash(lines, 4);
                btnUpPressedSinceMs = 0;
            }
        } else {
            btnUpPressedSinceMs = 0;
        }
    }
    // En modo AP el splash de "Connect to WorldTime-Setup..." se queda fijo
    // hasta que el user reconfigure y reinicie. Saltamos el render del reloj.
    if (WifiSetup::currentMode() == WifiSetup::Mode::Ap) {
        delay(50);
        return;
    }
    if (g_pendingReset) {
        delay(500);
        ESP.restart();
    }

    static uint32_t lastRender = 0;
    static uint8_t lastBrightness = 255;
    // Renderizamos a 20fps (50ms): suficientemente fluido para la barra
    // sub-pixel y el fade del colon, dejando mas slack a tasks de WiFi/HTTP
    // que vivan en core 1. Antes era 30fps; bajo a 20 tras observar latencia
    // alta en HTTP coincidente con el bump.
    if (millis() - lastRender < 50) {
        delay(5);
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
        const float FADE_SEC = 4.0f / 20.0f;   // 4 frames a 20fps = 200ms
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
        // Indicador de tendencia: comparamos forecast OM (siempre OM regardless
        // del provider activo, para baseline consistente) contra la temp OM
        // actual. STABLE si |Δ| < thresh1, sino RISING/FALLING con magnitud
        // por umbrales. Sin datos válidos → NONE.
        r.trendState = Display::Row::TrendState::TS_OFF;
        r.trendMagnitude = 0;
        if (Config::cfg.forecastIndicatorEnabled && d.hasForecast && d.hasOm) {
            int forecast = (Config::cfg.forecastIndicatorHorizonH == 2)
                               ? d.forecastT2h
                               : d.forecastT1h;
            float delta = (float)forecast - (float)d.tempC_om;
            float a = fabsf(delta);
            if (a < Config::cfg.forecastThresh1) {
                r.trendState = Display::Row::TrendState::TS_STABLE;
            } else {
                r.trendState = (delta > 0.0f) ? Display::Row::TrendState::TS_RISING
                                              : Display::Row::TrendState::TS_FALLING;
                int mag = 1;
                if (a >= Config::cfg.forecastThresh3)      mag = 3;
                else if (a >= Config::cfg.forecastThresh2) mag = 2;
                r.trendMagnitude = (int8_t)mag;
            }
        }
    }
    Display::renderRows(rows, secondOfMinuteF);
}
