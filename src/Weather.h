// Fetch de Open-Meteo y estado runtime por ciudad.
// La definicion de cuales ciudades existen vive en Config::cfg.cities[].
// Aqui solo guardamos el resultado (offset, temp, code, is_day).
#pragma once

#include <Arduino.h>

#include "Display.h"

namespace Weather {

struct Data {
    // Datos efectivos para render (recalculados de los crudos según provider activo).
    int offsetSec;          // siempre Open-Meteo
    int tempC;              // efectivo (om/tio/wap según tempSource)
    int code;               // efectivo
    bool isDay;             // siempre Open-Meteo
    bool hasData;           // hay algún dato disponible
    int tempSource;         // 0=none, 1=openmeteo, 2=tomorrow, 3=weatherapi

    // Datos crudos por proveedor (para fallback si el premium falla).
    int tempC_om, code_om;
    bool hasOm;
    int tempC_tio, code_tio;
    bool hasTio;            // alguna vez tuvimos datos de tio
    bool tioOk;             // ultimo fetch de tio fue exitoso. Si false, esta
                            // ciudad usa OM aunque tio esté activo, hasta que
                            // la rotacion vuelva a city y consiga exito.
    uint32_t tioOkAtMs;     // millis() del ultimo fetch TIO con exito. 0 = nunca
                            // (o tras reboot, hasta el primer exito). Se usa
                            // para considerar el dato stale si tiene > 1h.
    int tempC_wap, code_wap;
    bool hasWap;
    bool wapOk;
    uint32_t wapOkAtMs;     // misma logica que tioOkAtMs pero para WeatherAPI

    // Forecast horario (Open-Meteo): temperatura prevista a +1h y +2h respecto
    // a la hora actual UTC. Se usa para el indicador de tendencia. Siempre OM
    // (los providers premium en su tier free no dan hourly comoda).
    int  forecastT1h, forecastT2h;
    bool hasForecast;
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
extern FetchDebug debugInfo[4];     // por ciudad — Open-Meteo
extern FetchDebug debugInfoTio[4];  // por ciudad — Tomorrow.io (vacios si provider no activo)
extern FetchDebug debugInfoWap[4];  // por ciudad — WeatherAPI    (vacios si provider no activo)

void loadCache();          // Restaura data[] desde NVS (cache persistente).
void saveCache();          // Persiste data[] a NVS.
void taskStart();
bool fetchOneSync(int idx);                // fuerza fetch Open-Meteo sincrono
bool fetchOneTomorrowSync(int idx);        // fuerza fetch Tomorrow.io sincrono (si configurado)
bool fetchOneWeatherApiSync(int idx);      // fuerza fetch WeatherAPI sincrono (si configurado)
void requestRefresh();     // Despierta la task para refetch inmediato (no borra data anterior).
int  nextPremiumIdx();     // indice de la proxima ciudad de la rotacion premium (TIO o WAP)
inline int nextTioIdx() { return nextPremiumIdx(); }   // alias historico, mismo valor
Display::IconType::Value iconForCode(int code, bool isDay);

}  // namespace Weather
