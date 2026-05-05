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
    bool showColon;     // si false el ":" se omite (parpadeo)
};

void begin();
void setBrightness(uint8_t v);     // 0..255 hardware brightness
void renderRows(const Row rows[4]);
void clear();

uint16_t rgb888to565(uint32_t rgb);

// Preview: sobrescribe el icono de la fila 0 con frames ad-hoc (p.ej. icono
// en edicion). duration_ms=0 → indefinido; >0 → auto-stop tras ese tiempo
// (failsafe por si el navegador desconecta sin enviar stop).
void setIconPreview(const std::vector<Icons::Frame>& frames, uint32_t duration_ms);
void clearIconPreview();
bool isIconPreviewActive();

}  // namespace Display
