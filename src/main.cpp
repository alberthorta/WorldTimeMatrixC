#include <Arduino.h>
#include <ArduinoOTA.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>

#include "AutoUpdate.h"
#include "ClaudeStats.h"
#include "Config.h"
#include "Display.h"
#include "Icons.h"
#include "Version.h"
#include "WebApi.h"
#include "Weather.h"
#include "WifiSetup.h"

volatile bool g_pendingReset = false;

// Modo de visualizacion. Toggle con el boton central (TTP2/A3). Vive solo
// en RAM por ahora (no persiste tras reboot); migrar a Config si se valida.
//   FOUR_ROWS: render normal con las 4 ciudades.
//   FOCUS:     una sola ciudad (cities[0]) ocupando los 64x32 con HH:MM
//              grande, temp grande, icono x2 y barra de segundera.
enum class DisplayMode : uint8_t { FOUR_ROWS = 0, FOCUS = 1, CLAUDE = 2 };
static DisplayMode g_displayMode = DisplayMode::FOUR_ROWS;
static constexpr time_t TIME_VALID_THRESHOLD = 1672531200;   // 2023-01-01

// Flag set desde WebApi para simular pulsaciones desde el navegador. bit i
// = idx del boton (0=izq, 1=centro, 2=der). El loop lo consume y dispara
// la misma accion que el flanco PRESSED del TTP correspondiente.
static volatile uint8_t g_buttonRequestMask = 0;
static uint32_t g_brightnessDirtyMs = 0;
void requestButtonPress(int idx) {
    if (idx >= 0 && idx < 3) g_buttonRequestMask |= (1 << idx);
}

// Forward decls para handleButtonAction (definida tras inNightWindow).
static bool inNightWindow(uint16_t nowMins);
static DisplayMode nextDisplayMode(DisplayMode m);
// Centros del ripple para los 3 botones (mismas constantes que el TTP loop).
static const int16_t BTN_RIPPLE_X[3] = {16, Display::WIDTH / 2, Display::WIDTH - 16};

// Lanza la accion correspondiente al boton `idx` (0=izq, 1=centro, 2=der).
// Compartida entre el flanco PRESSED del TTP y la simulacion via WebApi.
// `source` es texto para los logs ("ttp" / "web").
static void handleButtonAction(int idx, const char* source) {
    if (idx < 0 || idx >= 3) return;
    Display::triggerRipple(idx, BTN_RIPPLE_X[idx], 0);
    if (idx == 1) {
        g_displayMode = nextDisplayMode(g_displayMode);
        const char* name =
            (g_displayMode == DisplayMode::FOCUS)  ? "FOCUS" :
            (g_displayMode == DisplayMode::CLAUDE) ? "CLAUDE" :
                                                     "FOUR_ROWS";
        Serial.printf("[%s] displayMode -> %s\n", source, name);
        if (g_displayMode == DisplayMode::CLAUDE) {
            ClaudeStats::requestRefresh();
        }
        return;
    }
    // Botones de brillo (izq/der). Si estamos en ventana de modo noche se
    // ajusta nightMode.brightness para que el cambio sea visible; sino el
    // brillo normal.
    float delta = (idx == 0) ? -0.05f : 0.05f;
    bool inNight = false;
    const auto& nm = Config::cfg.nightMode;
    if (nm.enabled) {
        time_t utc = time(nullptr);
        if (utc > TIME_VALID_THRESHOLD) {
            int refOffset = Weather::data[0].hasData
                                ? Weather::data[0].offsetSec : 0;
            time_t local = utc + refOffset;
            struct tm tm;
            gmtime_r(&local, &tm);
            uint16_t nowMins = tm.tm_hour * 60 + tm.tm_min;
            inNight = inNightWindow(nowMins);
        }
    }
    float& target = inNight ? Config::cfg.nightMode.brightness
                            : Config::cfg.brightness;
    float b = constrain(target + delta, 0.05f, 1.0f);
    if (b != target) {
        target = b;
        g_brightnessDirtyMs = millis();
        Serial.printf("[%s] %s -> %.2f\n", source,
                      inNight ? "night brightness" : "brightness", b);
    } else {
        Serial.printf("[%s] %s ya en limite (%.2f)\n", source,
                      inNight ? "night brightness" : "brightness", b);
    }
    Display::triggerBrightnessOverlay(target);
}

// Avanza al siguiente modo del toggle (boton central). CLAUDE solo si hay
// sessionKey configurada — sino se salta directamente.
static DisplayMode nextDisplayMode(DisplayMode m) {
    switch (m) {
        case DisplayMode::FOUR_ROWS: return DisplayMode::FOCUS;
        case DisplayMode::FOCUS:
            return ClaudeStats::isConfigured() ? DisplayMode::CLAUDE
                                                : DisplayMode::FOUR_ROWS;
        case DisplayMode::CLAUDE:    return DisplayMode::FOUR_ROWS;
    }
    return DisplayMode::FOUR_ROWS;
}

// Sensores tactiles TTP223 externos. Salida push-pull activa-HIGH, sin pull
// interno necesario. Tres botones en linea: izquierda / centro / derecha.
static constexpr uint8_t PIN_TTP1 = A2;  // GPIO 9   - izquierda
static constexpr uint8_t PIN_TTP2 = A3;  // GPIO 10  - centro
static constexpr uint8_t PIN_TTP3 = A4;  // GPIO 11  - derecha

// Estado del check periodico de auto-update. Se inicializa a "hace mucho"
// para que el primer check se dispare al boot (lo hace setup() explicitamente
// pero asi seguimos en sintonia tambien si se desactiva al boot y se reactiva
// despues).
static uint32_t g_autoUpdateLastCheckMs = 0;

// Hace un check de auto-update y, si hay nueva release, descarga+flash y
// reinicia. Bloquea hasta ~15s en el fetch y luego 1-3 min en el download
// si encuentra algo. El splash de busqueda solo se pinta si showSearchSplash
// es true (en el periodic check no queremos parpadeo si no hay update).
static void runAutoUpdateCheck(bool showSearchSplash) {
    if (WifiSetup::currentMode() != WifiSetup::Mode::Sta) return;
    Serial.printf("[autoupd] check (current=%s)\n", FW_VERSION);
    if (showSearchSplash) {
        const char* lines[] = {"WorldTime", "Checking", "update...", FW_VERSION};
        Display::drawSplash(lines, 4);
    }
    AutoUpdate::ReleaseInfo rel = AutoUpdate::fetchLatestRelease();
    g_autoUpdateLastCheckMs = millis();
    if (!rel.found) {
        Serial.println("[autoupd] no release info");
        return;
    }
    if (rel.tagName == String(FW_VERSION)) {
        Serial.printf("[autoupd] up to date (%s)\n", FW_VERSION);
        return;
    }
    Serial.printf("[autoupd] new release: %s (current %s)\n",
                  rel.tagName.c_str(), FW_VERSION);
    static char curBuf[20], newBuf[20], pctBuf[8];
    snprintf(curBuf, sizeof(curBuf), "v %s", FW_VERSION);
    snprintf(newBuf, sizeof(newBuf), "-> %s", rel.tagName.c_str());
    snprintf(pctBuf, sizeof(pctBuf), "0%%");
    {
        const char* lines[] = {"Updating", curBuf, newBuf, pctBuf};
        Display::drawSplash(lines, 4);
    }
    bool ok = AutoUpdate::downloadAndFlash(rel.binUrl, [](int pct) {
        Serial.printf("[autoupd] %d%%\n", pct);
        static int lastDrawn = -1;
        if (pct - lastDrawn < 5 && pct < 100) return;
        lastDrawn = pct;
        static char pb[8];
        snprintf(pb, sizeof(pb), "%d%%", pct);
        const char* lines[] = {"Updating", "downloading", "", pb};
        Display::drawSplash(lines, 4);
    });
    if (ok) {
        const char* okLines[] = {"Update OK", "Rebooting", "", ""};
        Display::drawSplash(okLines, 2);
        delay(1500);
        ESP.restart();
    } else {
        const char* failLines[] = {"Update failed", "Keeping", "current fw", ""};
        Display::drawSplash(failLines, 3);
        delay(2000);
    }
}

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
    pinMode(PIN_TTP1, INPUT);
    pinMode(PIN_TTP2, INPUT);
    pinMode(PIN_TTP3, INPUT);
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
        // Splash "IP : x.x.x.x" durante 2s antes de buscar update (reducido de
        // 5s; el auto-update ya da feedback visual si encuentra algo).
        char ipBuf[20];
        snprintf(ipBuf, sizeof(ipBuf), "%s", WifiSetup::currentIp().c_str());
        {
            const char* lines[] = {"WorldTime", "IP :", ipBuf, ""};
            Display::drawSplash(lines, 3);
        }
        delay(2000);

        // Auto-update via GitHub Releases (si habilitado desde la UI). Bloquea
        // hasta ~15s en el fetch y luego varios minutos si encuentra una
        // release nueva — el splash da feedback durante la descarga.
        if (Config::cfg.autoUpdateEnabled) {
            runAutoUpdateCheck(/*showSearchSplash=*/true);
        } else {
            Serial.println("[autoupd] disabled by config, skipping boot check");
        }
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
    ClaudeStats::loadCache();
    ClaudeStats::taskStart();
}

void loop() {
    ArduinoOTA.handle();
    WifiSetup::tickHealth();

    // Auto-update: periodic check + on-demand (boton en la web). Bloquea el
    // loop unos segundos mientras hace el fetch. Si nunca se llamo a
    // runAutoUpdateCheck (e.g. autoUpdateEnabled=false al boot y se reactiva
    // en runtime), g_autoUpdateLastCheckMs es 0 y forzamos un primer check.
    if (WifiSetup::currentMode() == WifiSetup::Mode::Sta) {
        bool requested = AutoUpdate::consumeCheckRequest();
        bool periodic  = false;
        if (Config::cfg.autoUpdateEnabled) {
            uint32_t intervalMs = (uint32_t)Config::cfg.autoUpdateCheckIntervalH
                                  * 3600UL * 1000UL;
            if (g_autoUpdateLastCheckMs == 0) {
                periodic = true;
            } else if ((millis() - g_autoUpdateLastCheckMs) >= intervalMs) {
                periodic = true;
            }
        }
        if (requested || periodic) {
            runAutoUpdateCheck(/*showSearchSplash=*/requested);
        }
    }

    // TTP223: lectura + edge-detect con filtrado por *tiempo continuo*. El
    // TTP223 ya filtra contacto humano, pero la cercania del HUB75 mete
    // bursts de ruido EMI que producen pulsos espurios cortos (1-10 ms).
    // En lugar de contar ticks (afectado por la frecuencia del loop), exijo
    // que el pin RAW se mantenga en el nuevo nivel un tiempo continuo:
    //   PRESS_HOLD_MS    = 80   → pin HIGH continuo 80ms para aceptar PRESSED
    //   RELEASE_HOLD_MS  = 40   → pin LOW continuo 40ms para aceptar release
    //   POST_PRESS_LOCK  = 350  → tras un PRESSED aceptado, ignora nuevos
    //                             PRESSED durante ese tiempo (anti-multiclick)
    // Una pulsacion humana dura >100ms con holgura, asi que 80ms se siente
    // instantaneo; un glitch de 5ms no llega ni cerca.
    //
    //   TTP1 (A2) izquierda -> brillo - 0.05
    //   TTP2 (A3) centro    -> toggle modo (FOUR_ROWS → FOCUS → CLAUDE)
    //   TTP3 (A4) derecha   -> brillo + 0.05
    static bool     ttp[3] = {false, false, false};
    static bool     ttpPrev[3] = {false, false, false};
    static bool     ttpRawPrev[3] = {false, false, false};
    static uint32_t ttpRawSinceMs[3] = {0, 0, 0};
    static uint32_t ttpLastPressMs[3] = {0, 0, 0};
    // g_brightnessDirtyMs es global para que handleButton pueda escribirla
    // tanto desde el TTP edge como desde el web simulado.
    constexpr uint32_t PRESS_HOLD_MS    = 80;
    constexpr uint32_t RELEASE_HOLD_MS  = 40;
    constexpr uint32_t POST_PRESS_LOCK  = 350;
    bool ttpRaw[3] = {
        digitalRead(PIN_TTP1) == HIGH,
        digitalRead(PIN_TTP2) == HIGH,
        digitalRead(PIN_TTP3) == HIGH
    };
    uint32_t now = millis();
    for (int i = 0; i < 3; i++) {
        if (ttpRaw[i] != ttpRawPrev[i]) {
            // El nivel raw acaba de cambiar: empieza a contar holding time.
            ttpRawSinceMs[i] = now;
            ttpRawPrev[i] = ttpRaw[i];
        }
        uint32_t held = now - ttpRawSinceMs[i];
        if (ttpRaw[i] && !ttp[i] && held >= PRESS_HOLD_MS) {
            bool tooSoon = (ttpLastPressMs[i] != 0) &&
                           (now - ttpLastPressMs[i] < POST_PRESS_LOCK);
            if (!tooSoon) {
                ttp[i] = true;
                ttpLastPressMs[i] = now;
            }
        } else if (!ttpRaw[i] && ttp[i] && held >= RELEASE_HOLD_MS) {
            ttp[i] = false;
        }
    }
    const uint8_t ttpPin[3] = {PIN_TTP1, PIN_TTP2, PIN_TTP3};
    const char* const ttpLabel[3] = {"A2/izq", "A3/cen", "A4/der"};
    const int16_t ttpRippleX[3] = {16, Display::WIDTH / 2, Display::WIDTH - 16};
    // Atender requests del web (simulacion de pulsacion desde la UI). Se
    // procesan antes del edge detect del TTP para que los logs salgan en
    // orden si ambos coinciden.
    uint8_t webReq = g_buttonRequestMask;
    g_buttonRequestMask = 0;
    for (int i = 0; i < 3; i++) {
        if (webReq & (1 << i)) {
            Serial.printf("[web] button %s\n", ttpLabel[i]);
            handleButtonAction(i, "web");
        }
    }
    // Edge detect de los TTPs fisicos.
    for (int i = 0; i < 3; i++) {
        if (ttp[i] != ttpPrev[i]) {
            Serial.printf("[ttp] TTP%d (%s, GPIO%d) %s\n",
                          i + 1, ttpLabel[i], ttpPin[i],
                          ttp[i] ? "PRESSED" : "released");
            if (ttp[i]) handleButtonAction(i, "ttp");
            ttpPrev[i] = ttp[i];
        }
    }
    // Persistencia diferida del brillo: 2s tras el ultimo cambio.
    if (g_brightnessDirtyMs != 0 && millis() - g_brightnessDirtyMs >= 2000) {
        Serial.printf("[cfg] persistiendo brillo %.2f\n", Config::cfg.brightness);
        Config::save();
        g_brightnessDirtyMs = 0;
    }

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
            if (Config::cfg.dateFormatText) {
                static const char* MESES_ES[12] = {
                    "Ene","Feb","Mar","Abr","May","Jun",
                    "Jul","Ago","Sep","Oct","Nov","Dic"
                };
                int mIdx = tm.tm_mon;
                if (mIdx < 0)  mIdx = 0;
                if (mIdx > 11) mIdx = 11;
                snprintf(nameBuffers[i], sizeof(nameBuffers[i]),
                         "%d %s", tm.tm_mday, MESES_ES[mIdx]);
            } else {
                snprintf(nameBuffers[i], sizeof(nameBuffers[i]),
                         "%02d/%02d", tm.tm_mday, tm.tm_mon + 1);
            }
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
            r.day = (uint8_t)tm.tm_mday;
            r.month = (uint8_t)(tm.tm_mon + 1);
        } else {
            r.hour = 0; r.minute = 0;
            r.day = 0; r.month = 0;
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
    if (g_displayMode == DisplayMode::FOCUS) {
        Display::renderFocus(rows[0], secondOfMinuteF);
    } else if (g_displayMode == DisplayMode::CLAUDE) {
        // Montamos ClaudeView desde ClaudeStats::data + computePace para los
        // dos windows. Si la sessionKey se ha borrado por la web, volvemos
        // a FOUR_ROWS para no quedarnos en un modo "inutil".
        if (!ClaudeStats::isConfigured()) {
            g_displayMode = DisplayMode::FOUR_ROWS;
            Display::renderRows(rows, secondOfMinuteF);
        } else {
            time_t now = utc;
            ClaudeStats::Pace p5 = ClaudeStats::computePace(
                ClaudeStats::data.fiveHour, 5L * 3600L, now);
            ClaudeStats::Pace p7 = ClaudeStats::computePace(
                ClaudeStats::data.sevenDay, 7L * 86400L, now);
            Display::ClaudeView cv;
            cv.hasData         = ClaudeStats::data.hasData;
            cv.fiveValid       = ClaudeStats::data.fiveHour.valid;
            cv.fiveUsed        = p5.used;
            cv.fiveElapsed     = p5.elapsed;
            cv.fiveRemainingSec = (long)(ClaudeStats::data.fiveHour.resetsAt - now);
            if (cv.fiveRemainingSec < 0) cv.fiveRemainingSec = 0;
            cv.fiveColor       = p5.color;
            cv.fiveLabel       = p5.label;
            cv.sevenValid      = ClaudeStats::data.sevenDay.valid;
            cv.sevenUsed       = p7.used;
            cv.sevenElapsed    = p7.elapsed;
            cv.sevenRemainingSec = (long)(ClaudeStats::data.sevenDay.resetsAt - now);
            if (cv.sevenRemainingSec < 0) cv.sevenRemainingSec = 0;
            cv.sevenColor      = p7.color;
            cv.sevenLabel      = p7.label;
            Display::renderClaude(rows[0], cv, secondOfMinuteF);
        }
    } else {
        Display::renderRows(rows, secondOfMinuteF);
    }
}
