// Persistencia de configuracion en NVS (Preferences).
// Layout:
//   wifi_ssid / wifi_pwd  -> claves separadas (NUNCA exportadas)
//   cfg                    -> JSON serializado con cities + brillo + modo noche + ...
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stdint.h>

namespace Config {

struct WifiCreds {
    String ssid;
    String password;
};

struct City {
    String name;        // <= 6 chars
    float lat;
    float lon;
    uint32_t colorRgb;  // 0xRRGGBB
};

struct NightMode {
    bool enabled;
    uint16_t startMins;     // minutos desde medianoche
    uint16_t endMins;
    float brightness;       // 0.05..1.0
};

// Indicador de segundos: alternativa entre el marcador (3px en la fila
// inferior) y la barra (columna vertical de altura completa por detras).
enum class SecondsIndicator : uint8_t {
    NONE = 0,
    MARKER = 1,    // 3 px sub-pixel en la fila inferior (look "playhead")
    BAR = 2,       // columna vertical de altura completa, por detras del contenido
};

struct All {
    float brightness;
    uint16_t weatherRefreshSec;
    bool colonBlink;
    bool hourLeadingZero;     // si false, "07:05" -> "7:05"
    bool dateFormatText;      // si true, $DATE pinta "D Mes" (ej "8 May") en
                              // lugar del default "DD/MM" ("08/05"). Mes en
                              // espanol abreviado a 3 letras.
    bool omIndicator;         // si true, dibuja un puntito debajo del º en filas
                              // que estan usando Open-Meteo (fallback Tio)
    SecondsIndicator secondsIndicator;
    uint32_t secondsBarColor; // 0xRRGGBB, color base de la barra vertical
                              // (default 0x333333; el marcador usa #444/#777
                              // hardcoded para mantener su look discreto).
    uint8_t  secondsBarWidth; // ancho de la barra vertical en px (1..16). 1 =
                              // columna fina con sub-pixel; >1 = bar mas
                              // ancha con flancos antialiased.
    bool     secondsBarProgress; // true → la zona ya recorrida queda pintada
                              // (estilo progressbar). false → solo la barra
                              // se mueve, sin estela. En progress mode el
                              // wrap toroidal queda desactivado.
    // Indicador de tendencia: mini barra vertical 2px a la derecha del º que
    // muestra si la temp va a subir (verde, hacia arriba) o bajar (rojo,
    // hacia abajo) en N horas, con 3 niveles de magnitud por umbrales.
    bool     forecastIndicatorEnabled;     // off por defecto
    uint8_t  forecastIndicatorHorizonH;    // 1 o 2 horas vista
    float    forecastThresh1;              // |Δ| ≥ thresh1 → 1 px
    float    forecastThresh2;              // |Δ| ≥ thresh2 → 2 px
    float    forecastThresh3;              // |Δ| ≥ thresh3 → 3 px
    uint32_t forecastColorRising;          // 0xRRGGBB (default 0x00C000)
    uint32_t forecastColorFalling;         // 0xRRGGBB (default 0xC00000)
    uint32_t forecastColorStable;          // 0xRRGGBB (default 0x666666)
    // Colores especificos del modo focus (boton central). Independientes del
    // color de la ciudad: en focus el contenido es uno solo, asi se puede
    // personalizar a gusto sin afectar al modo 4-filas.
    uint32_t focusHourColor;               // 0xRRGGBB (default 0xFFFFFF)
    uint32_t focusDateColor;               // 0xRRGGBB (default 0xAAAAAA)
    // Stats de Claude Code (claude.ai/api/organizations/.../usage). Si la
    // sessionKey esta vacia, el modo Claude no se ofrece en el toggle del
    // boton central. orgId se autodescubre desde la sessionKey en el primer
    // fetch exitoso y se persiste para no volver a llamarlo.
    String   claudeSessionKey;
    String   claudeOrgId;
    uint16_t claudeRefreshSec;             // 60..3600, default 180
    // Auto-update via GitHub Releases. Si enabled=false, ni se hace el check
    // al boot ni el check periodico. checkIntervalH: cada cuantas horas se
    // intenta despues del primer chequeo. 1..720 (1 mes).
    bool     autoUpdateEnabled;
    uint16_t autoUpdateCheckIntervalH;
    // Configuracion IP: DHCP por defecto. Si wifiUseDhcp=false aplicamos IP
    // estatica con los 5 campos antes de WiFi.begin. Strings vacias = no
    // setear ese campo (asi DNS secundario es opcional).
    bool     wifiUseDhcp;
    String   wifiStaticIp;
    String   wifiStaticGateway;
    String   wifiStaticSubnet;
    String   wifiStaticDns1;
    String   wifiStaticDns2;
    City cities[4];
    NightMode nightMode;
    uint32_t palette[16];     // 0xRRGGBB; slot 0 es transparente
    String rgbOrder;          // "RGB" o "RBG" (G/B intercambiados, segun panel)
};

extern All cfg;             // Instancia global (poblada por begin()).

// Snapshot de estado al boot, para diagnostico del flujo de persistencia.
// Histórico: en versiones previas el `cfg` se guardaba como blob en NVS, y eso
// fragmentaba la partición al cabo de muchos saves, llegando a corromper el
// namespace y perder claves (incl. wifi). Ahora el cfg vive en LittleFS y NVS
// retiene sólo wifi creds.
struct DiagInfo {
    uint32_t bootCount;
    // NVS legacy state (para migracion + visibilidad histórica)
    size_t cfgBlobLen;
    size_t cfgStringLen;
    size_t wifiSsidLen;
    size_t wifiPwdLen;
    size_t wxCacheLen;
    size_t nvsUsed;
    size_t nvsFree;
    size_t nvsTotal;
    size_t nvsNamespaces;
    String lastLoad;        // "seeded" | "loaded_fs" | "loaded_nvs_migrated" | "parse_fail:<reason>"
    // Captura del ultimo save(): util para detectar fallos silenciosos.
    uint32_t lastSaveCount;
    size_t lastSaveBytes;
    size_t lastSaveWrote;
    bool lastSaveOk;
    String lastSaveError;
    // FS state
    bool fsMounted;
    size_t fsCfgFileLen;
    size_t fsTotal;
    size_t fsUsed;
};
extern DiagInfo diag;
void captureNvsStatsLive();   // Re-captura nvsUsed/Free/Total desde fuera de begin().
void captureFsStatsLive();    // Re-captura tamaño de /cfg.json y stats LittleFS.

void begin();               // Carga desde NVS o siembra defaults.
bool save();                // Serializa cfg actual y persiste.

// Helpers para los handlers HTTP.
void writeJson(JsonDocument& doc);        // build JSON respuesta GET /api/config (incluye rgb_order para UI)
void writeBackupJson(JsonDocument& doc);  // backup exportable: SOLO lo que esta en cfg.json (sin claves NVS)
bool applyPatch(JsonDocument& doc);       // aplicar JSON parcial; true si cities cambian
bool setRgbOrder(const String& v);        // setter dedicado (NVS); endpoint /api/rgb_order

// Provider meteo "premium" (Tomorrow.io o WeatherAPI). Solo uno puede estar
// activo a la vez (exclusion mutua: activar uno desactiva el otro). Las claves
// y refresh de ambos persisten independientemente, asi se pueden alternar sin
// reescribir credenciales. Cuando hay activo Y key non-empty, Weather lo usa
// para temp+code; Open-Meteo siempre da offset+isDay y sirve de fallback.
struct TomorrowSettings {
    bool enabled;
    String apiKey;
    uint16_t refreshSec;   // intervalo independiente del de Open-Meteo
};
TomorrowSettings getTomorrowSettings();
void setTomorrowSettings(bool enabled, const String& apiKey, uint16_t refreshSec);
bool hasTomorrowSettings();   // shortcut: enabled && apiKey no vacio

struct WeatherApiSettings {
    bool enabled;
    String apiKey;
    uint16_t refreshSec;
};
WeatherApiSettings getWeatherApiSettings();
void setWeatherApiSettings(bool enabled, const String& apiKey, uint16_t refreshSec);
bool hasWeatherApiSettings();

// Cual de los dos proveedores premium esta activo (o NONE). Si por algun
// bug ambos quedaran enabled=true a la vez, gana Tomorrow.io.
enum class PremiumProvider { NONE, TOMORROW, WEATHERAPI };
PremiumProvider activePremium();

// WiFi (separado de cfg para no aparecer en backups JSON).
WifiCreds getWifi();
void setWifi(const String& ssid, const String& password);
void clearWifi();

}  // namespace Config
