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
#include "Jitter.h"
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
enum class DisplayMode : uint8_t { FOUR_ROWS = 0, FOCUS = 1, CLAUDE = 2, LIFE = 3, IMAGE = 4, FIRE = 5 };
static DisplayMode g_displayMode = DisplayMode::FOUR_ROWS;
static constexpr time_t TIME_VALID_THRESHOLD = 1672531200;   // 2023-01-01

// Estado del menu de botones (overlay). El boton central abre el MENU; en NORMAL
// izquierda/derecha cambian de modo. Ver handleButtonAction() para la maquina de
// estados completa.
//   NORMAL           : reloj; izq=modo anterior, der=modo siguiente, centro=menu
//   MENU             : izq/der mueven seleccion (Brillo/Jitter/Salir), centro ejecuta
//   MENU_BRIGHTNESS  : der=+brillo, izq=-brillo, centro=vuelve al menu
//   MENU_JITTER      : der=activa jitter, izq=desactiva, centro=vuelve al menu
enum class UiState : uint8_t { NORMAL, MENU, MENU_BRIGHTNESS, MENU_JITTER, MENU_HOLA, MENU_KEEPAWAKE };
static UiState  g_uiState = UiState::NORMAL;
static int      g_menuIndex = 0;             // 0=Brightness,1=Jitter,2=Session,3=Keep Awake,4=Exit
static constexpr int      MENU_COUNT = 5;
// Navegacion dentro de un submenu (Brillo/Jitter/Sesion). Modelo de 2 niveles:
//   - g_editing=false (navegar): izq/der mueven g_subIndex entre los campos +
//     la opcion "Atras" (siempre la ultima); centro entra a editar el campo, o
//     sale al menu si esta en "Atras".
//   - g_editing=true (editar): izq/der cambian el valor del campo; centro
//     acepta y vuelve a navegar.
static int  g_subIndex = 0;      // fila seleccionada dentro del submenu
static bool g_editing  = false;  // false=navegar, true=editar el campo actual

// Numero de campos editables (sin contar "Back") de cada submenu.
static int submenuFieldCount(UiState s) { return (s == UiState::MENU_HOLA) ? 3 : 1; }
static uint32_t g_uiActivityMs = 0;                 // ultimo input (auto-cierre)
static constexpr uint32_t MENU_TIMEOUT_MS = 20000;  // cierre por inactividad

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
static DisplayMode prevDisplayMode(DisplayMode m);
static const char* modeName(DisplayMode m);
static float& activeBrightnessTarget();   // ref al brillo efectivo (dia o noche)
// Centros del ripple para los 3 botones (mismas constantes que el TTP loop).
static const int16_t BTN_RIPPLE_X[3] = {16, Display::WIDTH / 2, Display::WIDTH - 16};

// Ajusta el brillo efectivo (dia o noche segun ventana) en +/- delta, con
// clamp 0.05..1.0 y marca de persistencia diferida. Usado por el submenu de
// brillo. No pinta el overlay clasico: el propio menu muestra la barra.
static void adjustActiveBrightness(float delta) {
    float& target = activeBrightnessTarget();
    float b = constrain(target + delta, 0.05f, 1.0f);
    if (b != target) {
        target = b;
        g_brightnessDirtyMs = millis();
    }
}

// Aplica un cambio (+dir/-dir) al campo `field` del submenu `s`. Los cambios se
// reflejan en vivo en Config::cfg (brillo dimea el panel, jitter arranca/para)
// para dar feedback inmediato; la persistencia ocurre al "aceptar".
static void submenuApplyDelta(UiState s, int field, int dir) {
    if (s == UiState::MENU_BRIGHTNESS) {
        adjustActiveBrightness(dir > 0 ? 0.05f : -0.05f);
    } else if (s == UiState::MENU_JITTER) {
        Config::cfg.jitterEnabled = (dir > 0);       // der=ON, izq=OFF
    } else if (s == UiState::MENU_KEEPAWAKE) {
        Config::cfg.claudeKeepAwakeEnabled = (dir > 0);   // der=ON, izq=OFF
    } else if (s == UiState::MENU_HOLA) {
        if (field == 0)      Config::cfg.claudeAutoHolaEnabled = (dir > 0);
        else if (field == 1) Config::cfg.claudeAutoHolaHour   =
                                 (uint8_t)((Config::cfg.claudeAutoHolaHour + dir + 24) % 24);
        else                 Config::cfg.claudeAutoHolaMinute =
                                 (uint8_t)((Config::cfg.claudeAutoHolaMinute + dir + 60) % 60);
    }
}

// Persiste los cambios del submenu al "aceptar" (centro en modo edicion) o al
// salir. Para el jitter ademas notifica a los suscriptores BLE.
static void submenuCommit(UiState s) {
    Config::save();
    if (s == UiState::MENU_JITTER) Jitter::notifyStatus();
}

// Lanza la accion correspondiente al boton `idx` (0=izq, 1=centro, 2=der),
// enrutada segun el estado del menu (g_uiState). Compartida entre el flanco
// PRESSED del TTP y la simulacion via WebApi. `source` es texto para logs.
static void handleButtonAction(int idx, const char* source) {
    if (idx < 0 || idx >= 3) return;
    // El flag ttpEnabled solo silencia el TTP fisico. Los botones de la web
    // funcionan siempre: el flag esta pensado para "tapar" un sensor fisico
    // que se dispara solo por ruido, sin perder el control remoto.
    bool fromTtp = (source && strcmp(source, "ttp") == 0);
    if (fromTtp && !Config::cfg.ttpEnabled[idx]) {
        Serial.printf("[ttp] button %d disabled, ignoring physical press\n", idx);
        return;
    }
    g_uiActivityMs = millis();   // cualquier input resetea el auto-cierre

    switch (g_uiState) {
    case UiState::NORMAL:
        // Ripple de feedback solo en el reloj (en el menu el propio resaltado
        // es la respuesta visual).
        Display::triggerRipple(idx, BTN_RIPPLE_X[idx], 0);
        if (idx == 1) {                       // centro: abrir menu
            g_uiState = UiState::MENU;
            g_menuIndex = 0;
            Serial.printf("[%s] menu abierto\n", source);
        } else {                              // izq=anterior, der=siguiente
            g_displayMode = (idx == 2) ? nextDisplayMode(g_displayMode)
                                       : prevDisplayMode(g_displayMode);
            Serial.printf("[%s] displayMode -> %s\n", source, modeName(g_displayMode));
            if (g_displayMode == DisplayMode::CLAUDE) ClaudeStats::requestRefresh();
        }
        break;

    case UiState::MENU:
        if (idx == 0) {                       // izquierda: opcion anterior
            g_menuIndex = (g_menuIndex + MENU_COUNT - 1) % MENU_COUNT;
        } else if (idx == 2) {                // derecha: opcion siguiente
            g_menuIndex = (g_menuIndex + 1) % MENU_COUNT;
        } else {                              // centro: entrar en la opcion
            if (g_menuIndex == 4) {           // Exit -> reloj
                g_uiState = UiState::NORMAL;
            } else {
                g_uiState = (g_menuIndex == 0) ? UiState::MENU_BRIGHTNESS
                          : (g_menuIndex == 1) ? UiState::MENU_JITTER
                          : (g_menuIndex == 2) ? UiState::MENU_HOLA
                                               : UiState::MENU_KEEPAWAKE;
                g_subIndex = 0;               // arranca navegando el 1er campo
                g_editing  = false;
            }
            Serial.printf("[%s] menu exec idx=%d\n", source, g_menuIndex);
        }
        break;

    // Submenus (Brightness/Jitter/Session/Keep Awake): modelo navegar -> entrar
    // -> editar -> aceptar, con "Back" como ultima opcion. Logica comun.
    case UiState::MENU_BRIGHTNESS:
    case UiState::MENU_JITTER:
    case UiState::MENU_KEEPAWAKE:
    case UiState::MENU_HOLA: {
        int nFields = submenuFieldCount(g_uiState);
        int nRows   = nFields + 1;            // + "Atras"
        int atras   = nFields;                // indice de "Atras"
        if (!g_editing) {                     // ── navegar ──
            if (idx == 0)      g_subIndex = (g_subIndex + nRows - 1) % nRows;
            else if (idx == 2) g_subIndex = (g_subIndex + 1) % nRows;
            else {                            // centro: entrar / salir
                if (g_subIndex == atras) {
                    g_uiState = UiState::MENU; // volver al menu principal
                    g_subIndex = 0;
                } else {
                    g_editing = true;         // entrar a editar el campo
                }
            }
        } else {                              // ── editar ──
            if (idx == 1) {                   // centro: aceptar
                submenuCommit(g_uiState);
                g_editing = false;
            } else {                          // izq/der: cambiar valor
                submenuApplyDelta(g_uiState, g_subIndex, idx == 2 ? +1 : -1);
            }
        }
        break;
    }
    }
}

// Avanza al siguiente modo del toggle (boton central). CLAUDE solo si hay
// sessionKey configurada — sino se salta directamente.
static DisplayMode nextDisplayMode(DisplayMode m) {
    switch (m) {
        case DisplayMode::FOUR_ROWS: return DisplayMode::FOCUS;
        case DisplayMode::FOCUS:
            return ClaudeStats::isConfigured() ? DisplayMode::CLAUDE
                                                : DisplayMode::LIFE;
        case DisplayMode::CLAUDE:    return DisplayMode::LIFE;
        case DisplayMode::LIFE:      return DisplayMode::IMAGE;
        case DisplayMode::IMAGE:     return DisplayMode::FIRE;
        case DisplayMode::FIRE:      return DisplayMode::FOUR_ROWS;
    }
    return DisplayMode::FOUR_ROWS;
}

// Inverso de nextDisplayMode (boton izquierdo). Mismo criterio con CLAUDE:
// si no hay sessionKey, se salta.
static DisplayMode prevDisplayMode(DisplayMode m) {
    switch (m) {
        case DisplayMode::FOUR_ROWS: return DisplayMode::FIRE;
        case DisplayMode::FOCUS:     return DisplayMode::FOUR_ROWS;
        case DisplayMode::CLAUDE:    return DisplayMode::FOCUS;
        case DisplayMode::LIFE:
            return ClaudeStats::isConfigured() ? DisplayMode::CLAUDE
                                               : DisplayMode::FOCUS;
        case DisplayMode::IMAGE:     return DisplayMode::LIFE;
        case DisplayMode::FIRE:      return DisplayMode::IMAGE;
    }
    return DisplayMode::FOUR_ROWS;
}

static const char* modeName(DisplayMode m) {
    switch (m) {
        case DisplayMode::FOCUS:  return "FOCUS";
        case DisplayMode::CLAUDE: return "CLAUDE";
        case DisplayMode::LIFE:   return "LIFE";
        case DisplayMode::IMAGE:  return "IMAGE";
        case DisplayMode::FIRE:   return "FIRE";
        default:                  return "FOUR_ROWS";
    }
}

// Los 3 botones tienen su GPIO y modo (pullup/no) configurables desde la
// web. Defaults: A2/A3/A4 con pull-up interno (pulsador a GND, activo LOW).
// Para sensores tipo TTP223 con salida push-pull activa-HIGH: desmarcar
// pullup desde la UI; entonces se usa INPUT plano y se lee activo en HIGH.

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

// Devuelve una referencia al brillo que esta "en efecto" ahora mismo: el de
// modo noche si estamos dentro de su ventana, sino el brillo de dia. Asi el
// submenu de brillo ajusta lo que realmente se ve en el panel.
static float& activeBrightnessTarget() {
    const auto& nm = Config::cfg.nightMode;
    if (nm.enabled) {
        time_t utc = time(nullptr);
        if (utc > TIME_VALID_THRESHOLD) {
            int refOffset = Weather::data[0].hasData ? Weather::data[0].offsetSec : 0;
            time_t local = utc + refOffset;
            struct tm tm;
            gmtime_r(&local, &tm);
            uint16_t nowMins = tm.tm_hour * 60 + tm.tm_min;
            if (inNightWindow(nowMins)) return Config::cfg.nightMode.brightness;
        }
    }
    return Config::cfg.brightness;
}

// Auto-"hola": una vez al dia local, a la hora configurada, dispara un
// openWindow (ClaudeStats) que abre/renueva la ventana de 5h. La hora es local
// (offset de cities[0], mismo criterio que modo noche y schedule). Se marca el
// dia local en cfg para no re-disparar tras un reboot. Portado de
// ClaudeStatsPortable (checkAutoOpen), aqui en local time.
static void checkAutoHola() {
    if (!Config::cfg.claudeAutoHolaEnabled) return;
    if (!ClaudeStats::isConfigured()) return;
    time_t utc = time(nullptr);
    if (utc <= TIME_VALID_THRESHOLD) return;        // NTP aun no sincronizado

    int refOffset = Weather::data[0].hasData ? Weather::data[0].offsetSec : 0;
    time_t local = utc + refOffset;
    struct tm tm;
    gmtime_r(&local, &tm);
    uint32_t today = (uint32_t)(tm.tm_year + 1900) * 10000u
                   + (uint32_t)(tm.tm_mon + 1) * 100u
                   + (uint32_t)tm.tm_mday;
    if (today == Config::cfg.claudeAutoHolaLastDate) return;   // ya disparado hoy

    uint16_t nowMins  = tm.tm_hour * 60 + tm.tm_min;
    uint16_t fireMins = Config::cfg.claudeAutoHolaHour * 60 + Config::cfg.claudeAutoHolaMinute;
    if (nowMins < fireMins) return;                 // aun no es la hora

    Serial.printf("[hola] auto-disparo %04u-%02u-%02u %02u:%02u local\n",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  Config::cfg.claudeAutoHolaHour, Config::cfg.claudeAutoHolaMinute);
    ClaudeStats::requestOpenWindow();
    // Marcar el dia hecho pase lo que pase (un fallo transitorio no debe
    // spammear claude.ai). Un intento al dia.
    Config::cfg.claudeAutoHolaLastDate = today;
    Config::save();
}

// Keep-awake: mantiene viva la sesion de Claude re-abriendo la ventana de 5h
// en cuanto deja de haber una activa.
//
// Ojo con la condicion de disparo, que es donde estaba el bug original: la
// primera version exigia ver un resets_at PASADO ("ventana expirada"). Pero
// claude.ai deja de mandar resets_at en cuanto la ventana se cierra, asi que
// resets_at pasaba a 0 y el guard `resetsAt <= 0` salia sin disparar. La unica
// forma de ver un resets_at pasado era pillar los pocos segundos entre que
// vence y que el servidor lo retira, con un polling de 60s: en la practica no
// disparaba nunca.
//
// Ahora la unica condicion POSITIVA es "ventana viva" = hay resets_at y esta en
// el futuro. Cualquier otra cosa (sin five_hour, sin resets_at, o resets_at ya
// pasado) cuenta como que no hay ventana y toca abrir una.
static constexpr uint32_t KEEPAWAKE_RETRY_MS = 5 * 60 * 1000;   // reintento si no abre
static uint32_t g_keepAwakeNextTryMs = 0;   // 0 = armado, dispara en cuanto toque

static void checkKeepAwake() {
    if (!Config::cfg.claudeKeepAwakeEnabled) return;
    if (!ClaudeStats::isConfigured()) return;
    if (ClaudeStats::data.holaStatus == ClaudeStats::HolaStatus::PENDING) return;
    time_t now = time(nullptr);
    if (now <= TIME_VALID_THRESHOLD) return;                // NTP aun no listo
    // Sin ningun dato (ni fetch ni cache) no sabemos si hay ventana; no
    // disparamos a ciegas, que si no un arranque con claude.ai caido mandaria
    // un hola por cada reboot.
    if (!ClaudeStats::data.hasData) return;

    const auto& w = ClaudeStats::data.fiveHour;
    if (w.valid && w.resetsAt > 0 && now < w.resetsAt) {
        g_keepAwakeNextTryMs = 0;      // ventana viva: rearmar para la proxima
        return;
    }

    // Cooldown: si el hola no consigue abrir ventana (o falla), reintentamos
    // cada KEEPAWAKE_RETRY_MS en vez de en cada vuelta del loop.
    if (g_keepAwakeNextTryMs != 0 &&
        (int32_t)(millis() - g_keepAwakeNextTryMs) < 0) return;

    Serial.printf("[keepawake] sin ventana 5h activa (valid=%d reset=%ld) -> hola\n",
                  (int)w.valid, (long)w.resetsAt);
    ClaudeStats::requestOpenWindow();
    g_keepAwakeNextTryMs = millis() + KEEPAWAKE_RETRY_MS;
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
    // Pinmode inicial segun Config (0=INPUT, 1=INPUT_PULLUP, 2=INPUT_PULLDOWN).
    // Se reaplica en el loop si la config cambia desde la web.
    for (int i = 0; i < 3; i++) {
        uint8_t mode = Config::cfg.ttpPinMode[i];
        pinMode(Config::cfg.ttpPin[i],
                mode == 1 ? INPUT_PULLUP :
                mode == 2 ? INPUT_PULLDOWN : INPUT);
    }
    Icons::begin();          // Defaults (nombres + frame inicial); Config los sobrescribe.
    Config::begin();
    // Modo inicial: respeta cfg.startupMode. Si pide CLAUDE pero no hay
    // sessionKey configurada, fallback a FOUR_ROWS para no quedarnos en
    // un modo sin contenido.
    {
        DisplayMode want = (DisplayMode)Config::cfg.startupMode;
        if (want == DisplayMode::CLAUDE && !ClaudeStats::isConfigured()) {
            want = DisplayMode::FOUR_ROWS;
        }
        g_displayMode = want;
    }
    Weather::loadCache();   // muestra ultima meteo conocida mientras NTP/fetch arrancan
    Display::reloadUserImage();   // carga /userimg.bin si existe (modo IMAGE)
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
    // Raton BLE HID + servicio de control del jitter. Se inicializa siempre
    // (coexiste con WiFi STA o AP); el motor solo mueve el cursor si el jitter
    // esta activo en config Y hay un host BLE emparejado. Va el ultimo para no
    // retrasar el arranque del reloj/WiFi.
    Jitter::begin();
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
    // Re-aplica pinMode si el GPIO o el modo cambian desde la web. La
    // polaridad sigue al modo: PULLUP (1) → activo LOW. INPUT (0) y
    // PULLDOWN (2) → activo HIGH (TTP223 push-pull o pulsador a 3V3).
    static uint8_t s_lastPin[3]  = {0xFF, 0xFF, 0xFF};
    static uint8_t s_lastMode[3] = {0xFF, 0xFF, 0xFF};
    bool ttpRaw[3];
    for (int i = 0; i < 3; i++) {
        uint8_t pin  = Config::cfg.ttpPin[i];
        uint8_t mode = Config::cfg.ttpPinMode[i];
        if (pin != s_lastPin[i] || mode != s_lastMode[i]) {
            pinMode(pin, mode == 1 ? INPUT_PULLUP :
                         mode == 2 ? INPUT_PULLDOWN : INPUT);
            s_lastPin[i]  = pin;
            s_lastMode[i] = mode;
        }
        bool activeLow = (mode == 1);
        ttpRaw[i] = digitalRead(pin) == (activeLow ? LOW : HIGH);
    }
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
    const uint8_t ttpPin[3] = {Config::cfg.ttpPin[0], Config::cfg.ttpPin[1], Config::cfg.ttpPin[2]};
    const char* const ttpLabel[3] = {"izq", "cen", "der"};
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

    // Auto-cierre del menu por inactividad: si no se toca ningun boton en
    // MENU_TIMEOUT_MS, volvemos al reloj para no quedarnos colgados en el menu.
    if (g_uiState != UiState::NORMAL &&
        millis() - g_uiActivityMs >= MENU_TIMEOUT_MS) {
        // Si el cierre pilla editando un campo, persistir el valor en curso.
        if (g_editing) { submenuCommit(g_uiState); g_editing = false; }
        g_uiState = UiState::NORMAL;
        Serial.println("[menu] auto-cierre por inactividad");
    }

    // Auto-"hola" diario (abre la ventana de 5h de Claude a la hora fijada).
    checkAutoHola();
    // Keep-awake: re-abre la ventana en cuanto expira.
    checkKeepAwake();

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

    // Programaciones de cambio de modo. Comprobamos en la transicion de
    // minuto local (referencia de cities[0]). lastScheduleMin guarda el
    // ultimo minuto procesado para no disparar varias veces dentro del
    // mismo minuto.
    if (timeOk) {
        static int lastScheduleMin = -1;
        time_t local = utc + refOffset;
        struct tm tm;
        gmtime_r(&local, &tm);
        int curMin = tm.tm_hour * 60 + tm.tm_min;
        if (curMin != lastScheduleMin) {
            lastScheduleMin = curMin;
            for (int i = 0; i < Config::SCHEDULE_MAX; i++) {
                const auto& s = Config::cfg.schedule[i];
                if (!s.enabled) continue;
                if ((int)(s.hour * 60 + s.minute) != curMin) continue;
                DisplayMode want = (DisplayMode)s.mode;
                if (want == DisplayMode::CLAUDE && !ClaudeStats::isConfigured()) {
                    Serial.printf("[sched] %02u:%02u CLAUDE skipped (no sessionKey)\n",
                                  s.hour, s.minute);
                    continue;
                }
                Serial.printf("[sched] %02u:%02u -> mode %u\n",
                              s.hour, s.minute, s.mode);
                g_displayMode = want;
                if (want == DisplayMode::CLAUDE) ClaudeStats::requestRefresh();
            }
        }
    }

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
    if (g_uiState != UiState::NORMAL) {
        // El menu de botones tapa el reloj mientras esta abierto.
        Display::MenuState ms;
        ms.view = (g_uiState == UiState::MENU_BRIGHTNESS) ? Display::MenuView::BRIGHTNESS
                : (g_uiState == UiState::MENU_JITTER)     ? Display::MenuView::JITTER
                : (g_uiState == UiState::MENU_HOLA)       ? Display::MenuView::HOLA
                : (g_uiState == UiState::MENU_KEEPAWAKE)  ? Display::MenuView::KEEPAWAKE
                                                          : Display::MenuView::MAIN;
        // En el menu principal la fila es g_menuIndex; en un submenu es g_subIndex.
        ms.selected        = (g_uiState == UiState::MENU) ? g_menuIndex : g_subIndex;
        ms.editing         = g_editing;
        ms.brightness      = activeBrightnessTarget();
        ms.jitterEnabled   = Config::cfg.jitterEnabled;
        ms.jitterConnected = Jitter::hostConnected();
        ms.holaEnabled     = Config::cfg.claudeAutoHolaEnabled;
        ms.holaHour        = Config::cfg.claudeAutoHolaHour;
        ms.holaMinute      = Config::cfg.claudeAutoHolaMinute;
        ms.keepAwakeEnabled = Config::cfg.claudeKeepAwakeEnabled;
        Display::renderMenu(ms);
    } else if (g_displayMode == DisplayMode::FOCUS) {
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
            ClaudeStats::Pace pf = ClaudeStats::computePace(
                ClaudeStats::data.fable, 7L * 86400L, now);
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
            cv.fableValid      = ClaudeStats::data.fable.valid;
            cv.fableUsed       = pf.used;
            cv.fableElapsed    = pf.elapsed;
            cv.fableColor      = pf.color;
            Display::renderClaude(rows[0], cv, secondOfMinuteF);
        }
    } else if (g_displayMode == DisplayMode::LIFE) {
        Display::renderLife(rows[0], secondOfMinuteF);
    } else if (g_displayMode == DisplayMode::IMAGE) {
        Display::renderImage(rows[0], secondOfMinuteF);
    } else if (g_displayMode == DisplayMode::FIRE) {
        Display::renderFire(rows[0], secondOfMinuteF);
    } else {
        Display::renderRows(rows, secondOfMinuteF);
    }
}
