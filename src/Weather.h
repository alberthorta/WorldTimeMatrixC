// Fetch de Open-Meteo y estado runtime por ciudad.
// La definicion de cuales ciudades existen vive en Config::cfg.cities[].
// Aqui solo guardamos el resultado (offset, temp, code, is_day).
#pragma once

#include <Arduino.h>

#include "Display.h"

namespace Weather {

struct Data {
    int offsetSec;
    int tempC;
    int code;
    bool isDay;
    bool hasData;
};

extern Data data[4];

struct FetchDebug {
    int httpCode = -1;
    int attempts = 0;
    String lastError;
    String lastUrl;     // URL llamada en el ultimo fetch
    String lastBody;    // respuesta raw (truncada a 2KB para acotar RAM)
    uint32_t lastAtMs = 0;  // millis() al final del ultimo fetch
};
extern FetchDebug debugInfo[4];   // una entrada por ciudad

void loadCache();          // Restaura data[] desde NVS (cache persistente).
void saveCache();          // Persiste data[] a NVS.
void taskStart();
bool fetchOneSync(int idx);
void requestRefresh();     // Despierta la task para refetch inmediato (no borra data anterior).
Display::IconType::Value iconForCode(int code, bool isDay);

}  // namespace Weather
