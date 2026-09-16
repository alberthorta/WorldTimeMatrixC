// Fetch de estadisticas de Claude Code (claude.ai/api/organizations/.../usage)
// usando la sessionKey configurada por el usuario.
//
// Inspirado en el proyecto hermano ClaudeStatsPortable (LilyGo T-Display S3):
//   https://github.com/alberthorta/ClaudeStatsPortable
//
// Mismas reglas de "pace" que la app macOS y la version portable: ratio
// utilization/elapsed con thresholds 0.75 / 0.95 / 1.10 / 1.35.
#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace ClaudeStats {

struct UsageWindow {
    bool   valid = false;
    double utilization = 0.0;   // 0..100 (% segun API)
    time_t resetsAt = 0;        // epoch UTC del proximo reset
};

// Estado del ultimo "hola" (openWindow) para mostrarlo en web/menu.
enum class HolaStatus : uint8_t { NONE = 0, PENDING = 1, OK = 2, FAIL = 3 };

struct Data {
    bool        hasData = false;     // al menos un fetch exitoso desde boot/cache
    UsageWindow fiveHour;
    UsageWindow sevenDay;
    // Cap semanal propio de un modelo: la entrada "weekly_scoped" del array
    // "limits" cuyo scope.model.display_name es Fable. Misma ventana de 7 dias
    // que sevenDay, asi que comparte reset y elapsed.
    UsageWindow fable;
    uint32_t    lastOkAtMs = 0;       // millis() del ultimo fetch ok
    String      lastError;            // texto del ultimo error (o vacio)
    // Auto-"hola" (openWindow)
    HolaStatus  holaStatus = HolaStatus::NONE;
    uint32_t    holaAtMs = 0;         // millis() del ultimo intento
    String      holaError;            // texto del ultimo error de hola (o vacio)
};

extern Data data;

// Modelo del pace: ratio used/elapsed con thresholds:
//   < 0.75 : "Well under" (verde)
//   < 0.95 : "Under"      (mint)
//   < 1.10 : "On pace"    (amarillo)
//   < 1.35 : "Over"       (naranja)
//   else   : "Burning"    (rojo)
struct Pace {
    double      used    = 0;   // 0..1
    double      elapsed = 0;   // 0..1
    double      ratio   = 0;
    const char* label   = "";
    uint32_t    color   = 0xFFFFFF;   // RGB888
};

Pace computePace(const UsageWindow& w, long totalSeconds, time_t now);
// Formato compacto para countdown ("3h22m" / "4d12h").
String formatCountdown(time_t resetsAt, time_t now, bool isWeekly);

void loadCache();
void saveCache();
void taskStart();
void requestRefresh();

// Dispara un "hola" (openWindow) en la task de fondo: crea una conversacion,
// manda un completion minimo "hola" (esto abre/renueva la ventana de 5h) y la
// borra. No bloquea: encola el trabajo y despierta la task. El estado queda en
// data.holaStatus / data.holaError.
void requestOpenWindow();

// True si hay una sessionKey configurada (toggle del modo en main mira esto).
bool isConfigured();

}  // namespace ClaudeStats
