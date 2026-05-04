// Iconos animables para el panel.
// Cada icono = lista de frames; cada frame = 5x5 pixeles indexados a una paleta de 16 colores.
// Phase 5: defaults hardcoded, single-frame. Phase 6 los hara editables via UI.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stdint.h>
#include <vector>

namespace Icons {

constexpr int NUM_ICONS = 9;
constexpr int PALETTE_SIZE = 16;
constexpr int ICON_W = 5;
constexpr int ICON_H = 5;

struct Frame {
    uint8_t px[ICON_H][ICON_W];   // indices 0..PALETTE_SIZE-1, 0 = transparente
    uint16_t ms;
};

struct IconDef {
    String name;
    std::vector<Frame> frames;
};

extern IconDef icons[NUM_ICONS];
extern const uint32_t DEFAULT_PALETTE[PALETTE_SIZE];

void begin();                                   // Carga defaults (icons + paleta).
const IconDef* find(const char* name);
uint16_t paletteAs565(int idx);                 // Lee desde Config::cfg.palette.
int indexFromName(const char* name);            // -1 si no existe.

void serializeAll(JsonObject obj);              // Para /api/config GET.
void deserializeAll(JsonObjectConst obj);       // Para /api/config POST.

}  // namespace Icons
