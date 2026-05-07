// Driver del panel HUB75 64x32. Encapsula HUB75-DMA + estado de render.
//
// Phase 2: 4 filas hardcoded para validar pipeline. En fases siguientes esto
// usara cities/weather del Config.
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <vector>

#include "Icons.h"

namespace Display {

constexpr int WIDTH = 64;
constexpr int HEIGHT = 32;

// Indices coinciden con Icons::icons[] (orden: SUN, PARTLY, CLOUD, RAIN, SNOW, STORM, FOG, MOON, PARTLY_NIGHT).
struct IconType {
    enum Value : int8_t {
        NONE = -1,
        SUN = 0, PARTLY, CLOUD, RAIN, SNOW, STORM, FOG, MOON, PARTLY_NIGHT
    };
};

struct Row {
    const char* name;
    uint8_t hour;
    uint8_t minute;
    int8_t tempC;
    IconType::Value icon;
    uint16_t color;     // 565 RGB para nombre/hora
    bool hasTime;       // si false, no se pinta HH:MM (NTP aun sin sincronizar)
    bool hasWeather;    // si false, no se pinta icono ni temp
    float colonAlpha;   // 0..1; controla la intensidad del ":" (parpadeo con
                        // fade-in/out cuando colonBlink esta activo). 1 = full,
                        // 0 = no se ve.
    bool omIndicator;   // si true, dibuja un puntito gris debajo del º (señal
                        // visible de que esa fila esta usando datos OM —
                        // util cuando Tio esta activo y quieres saber si hay
                        // fallback o no).
    // Indicador de tendencia (mini barra a la derecha del º): 0 → no dibuja;
    // 1..3 → magnitud (px hacia arriba/abajo). trendRising da el sentido y
    // por tanto el color (verde sube / rojo baja). Computado en main.cpp con
    // Config::cfg umbrales y los forecast de Weather::Data.
    int8_t trendMagnitude;
    bool   trendRising;
};

void begin();
void setBrightness(uint8_t v);     // 0..255 hardware brightness
// `secondOfMinuteF` en float (0..60, con fraccion sub-segundo) para que la
// barra de segundos se mueva continua a la velocidad de render del loop.
// < 0 → sin tiempo valido, no se dibuja.
void renderRows(const Row rows[4], float secondOfMinuteF = -1.0f);
void clear();

uint16_t rgb888to565(uint32_t rgb);

// Preview: sobrescribe el icono de la fila 0 con frames ad-hoc (p.ej. icono
// en edicion). duration_ms=0 → indefinido; >0 → auto-stop tras ese tiempo
// (failsafe por si el navegador desconecta sin enviar stop).
void setIconPreview(const std::vector<Icons::Frame>& frames, uint32_t duration_ms);
void clearIconPreview();
bool isIconPreviewActive();

}  // namespace Display
