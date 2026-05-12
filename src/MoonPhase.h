// Fase lunar global. La fase depende del angulo Sol-Tierra-Luna, que cambia
// ~12 grados/dia; en un instante dado todo el planeta ve la misma fase
// (variacion entre ciudades = horas, irrelevante para 4 buckets discretos).
// Por eso no se calcula por lat/lon — solo necesitamos UTC.
#pragma once

#include <stdint.h>
#include <time.h>

namespace MoonPhase {

enum Phase : int8_t {
    NEW = 0,
    WAXING = 1,
    FULL = 2,
    WANING = 3,
};

// Mes sinodico (new->new) en dias. Aceptable ±0.01% para nuestros buckets.
constexpr double SYNODIC_DAYS = 29.530588853;

// Devuelve la edad de la lunacion actual en dias [0, SYNODIC_DAYS).
// `now` es Unix epoch UTC (time(nullptr) si NTP esta sync).
double ageDays(time_t now);

// Mapea edad a una de 4 fases. NEW/FULL ocupan una ventana de
// (2 * halfWindowDays) centrada en su evento; el resto cae en WAXING o WANING.
// Default halfWindowDays = 1.5 -> ventana de 3 dias para llena/nueva.
Phase bucketize(double ageDays, double halfWindowDays = 1.5);

const char* name(Phase p);     // "NEW" / "WAXING" / "FULL" / "WANING"
const char* suffix(Phase p);   // "_NEW" / "_WAXING" / "_FULL" / "_WANING"

}  // namespace MoonPhase
