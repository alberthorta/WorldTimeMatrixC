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

struct Data {
    bool        hasData = false;     // al menos un fetch exitoso desde boot/cache
    UsageWindow fiveHour;
    UsageWindow sevenDay;
    uint32_t    lastOkAtMs = 0;       // millis() del ultimo fetch ok
    String      lastError;            // texto del ultimo error (o vacio)
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

// True si hay una sessionKey configurada (toggle del modo en main mira esto).
bool isConfigured();

}  // namespace ClaudeStats
