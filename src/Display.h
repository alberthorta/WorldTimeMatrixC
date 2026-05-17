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
    // Indicador de tendencia (mini bar a la derecha del º). 4 estados:
    //   NONE     → no se dibuja
    //   STABLE   → "=" (2 px arriba y 2 abajo del centro), color stable
    //   RISING   → bar 1..3 px hacia arriba, color rising
    //   FALLING  → bar 1..3 px hacia abajo, color falling
    // Computado en main.cpp con Config::cfg umbrales y los forecast de Weather.
    // Nota: usamos prefijo TS_ porque RISING/FALLING son macros de Arduino.h
    // (interrupt modes), incluso dentro de enum class causarian conflicto al
    // pre-procesado.
    enum class TrendState : int8_t { TS_OFF = 0, TS_STABLE = 1, TS_RISING = 2, TS_FALLING = 3 };
    TrendState trendState;
    int8_t trendMagnitude;     // solo para RISING/FALLING (1..3)
};

void begin();
void setBrightness(uint8_t v);     // 0..255 hardware brightness
// `secondOfMinuteF` en float (0..60, con fraccion sub-segundo) para que la
// barra de segundos se mueva continua a la velocidad de render del loop.
// < 0 → sin tiempo valido, no se dibuja.
void renderRows(const Row rows[4], float secondOfMinuteF = -1.0f);
// Modo focus: dibuja UNA sola ciudad ocupando los 64x32 con HH:MM grande, temp
// grande, icono escalado y barra de segundera abajo. Pensado para resaltar la
// primera ciudad de la lista. Toggle desde el boton central.
void renderFocus(const Row& row, float secondOfMinuteF = -1.0f);
// Modo Claude stats: muestra utilizacion 5h + 7d con pace bars y countdown.
// Recibe la primera ciudad para pintar tambien una hora + icono + temp
// compactos en la parte superior.
struct ClaudeView {
    bool hasData;
    bool fiveValid;
    double fiveUsed;        // 0..1
    double fiveElapsed;     // 0..1
    long   fiveRemainingSec;
    uint32_t fiveColor;     // RGB888 (pace color)
    const char* fiveLabel;
    bool sevenValid;
    double sevenUsed;
    double sevenElapsed;
    long   sevenRemainingSec;
    uint32_t sevenColor;
    const char* sevenLabel;
};
void renderClaude(const Row& weatherRow, const ClaudeView& cv, float secondOfMinuteF = -1.0f);
void clear();
// Pinta toda la pantalla del color RGB565 indicado (con flip de buffer).
void fillScreen(uint16_t color565);
// Dispara un ripple ("gota") centrado en (cx, cy). Se dibuja como overlay
// encima del render del reloj durante ~700ms: un anillo que crece de radio 1
// a ~40px y se desvanece. Llamar en el flanco PRESSED del boton, no en cada
// frame mientras esta pulsado.
//   slot: 0..1, permite tener dos ripples simultaneos (A2 y A3).
void triggerRipple(int slot, int16_t cx, int16_t cy);
// Muestra un overlay temporal con label "BRIGHTNESS", barra de progreso y
// porcentaje. Cada llamada renueva el timer (timeout 1.5s sin nuevos pulsos).
// `value` debe ser 0..1.
void triggerBrightnessOverlay(float value);
// Splash de boot: hasta 4 lineas centradas horizontalmente en sus rows.
// Lineas vacias se omiten. Util para "WorldTime" + estado de WiFi en boot.
void drawSplash(const char* const lines[], int nLines);

uint16_t rgb888to565(uint32_t rgb);

// Preview: sobrescribe el icono de la fila 0 con frames ad-hoc (p.ej. icono
// en edicion). duration_ms=0 → indefinido; >0 → auto-stop tras ese tiempo
// (failsafe por si el navegador desconecta sin enviar stop).
void setIconPreview(const std::vector<Icons::Frame>& frames, uint32_t duration_ms);
void clearIconPreview();
bool isIconPreviewActive();

}  // namespace Display
