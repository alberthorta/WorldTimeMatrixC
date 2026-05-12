#include "MoonPhase.h"

#include <math.h>

namespace MoonPhase {

// Referencia: luna nueva 2000-01-06 18:14:00 UTC = Unix 947182440.
// (Epoca standard usada en libros de astronomia practica.)
static constexpr double REF_NEW_MOON_UNIX = 947182440.0;
static constexpr double FULL_MOON_AGE = SYNODIC_DAYS / 2.0;  // ~14.7653 dias

double ageDays(time_t now) {
    double days = ((double)now - REF_NEW_MOON_UNIX) / 86400.0;
    double age = fmod(days, SYNODIC_DAYS);
    if (age < 0) age += SYNODIC_DAYS;
    return age;
}

Phase bucketize(double age, double half) {
    // NEW envuelve el cero: [SYNODIC - half, SYNODIC) U [0, half)
    if (age < half || age >= (SYNODIC_DAYS - half)) return NEW;
    if (age >= (FULL_MOON_AGE - half) && age < (FULL_MOON_AGE + half)) return FULL;
    if (age < FULL_MOON_AGE) return WAXING;
    return WANING;
}

const char* name(Phase p) {
    switch (p) {
        case NEW:    return "NEW";
        case WAXING: return "WAXING";
        case FULL:   return "FULL";
        case WANING: return "WANING";
    }
    return "?";
}

const char* suffix(Phase p) {
    switch (p) {
        case NEW:    return "_NEW";
        case WAXING: return "_WAXING";
        case FULL:   return "_FULL";
        case WANING: return "_WANING";
    }
    return "";
}

}  // namespace MoonPhase
