#include "Display.h"

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Fonts/TomThumb.h>

#include "Config.h"
#include "Icons.h"

namespace Display {

// Pin mapping del Adafruit Matrix Portal S3 (mismos GPIO que `board.MTX_*` en
// CircuitPython). El cableado RGB depende del panel concreto: algunos siguen
// el orden estandar R-G-B, otros tienen G y B intercambiados. Configurable via
// Config::cfg.rgbOrder.
static constexpr int8_t PIN_R1_GPIO = 42;
static constexpr int8_t PIN_G1_GPIO = 41;
static constexpr int8_t PIN_B1_GPIO = 40;
static constexpr int8_t PIN_R2_GPIO = 38;
static constexpr int8_t PIN_G2_GPIO = 39;
static constexpr int8_t PIN_B2_GPIO = 37;
static constexpr int8_t PIN_A = 45;
static constexpr int8_t PIN_B = 36;
static constexpr int8_t PIN_C = 48;
static constexpr int8_t PIN_D = 35;
static constexpr int8_t PIN_E = -1;        // sin ADDRE en 1/16 scan
static constexpr int8_t PIN_LAT = 47;
static constexpr int8_t PIN_OE = 14;
static constexpr int8_t PIN_CLK = 2;

static MatrixPanel_I2S_DMA* dma = nullptr;

// Layout: 4 filas (8px cada). Texto centrado verticalmente con tom-thumb (3x6).
// `ROW_YS` es el baseline que usa Adafruit_GFX para print (dibuja por encima).
static constexpr int ROW_YS[4] = {6, 14, 22, 30};
static constexpr int DIV_YS[3] = {7, 15, 23};
static constexpr int ICON_Y_OFFSET = -5;            // desde baseline -> top del icono
static constexpr int NAME_X = 1;

// Layout del bloque "HH:MM": right-aligned. TIME_RIGHT_X es la posicion final
// fija del bloque (donde caia el ultimo digito con la antigua rejilla monospace
// 4px/digito). Cuando aparecen "1"s, el bloque se compacta hacia la derecha
// creciendo por la izquierda.
static constexpr int TIME_RIGHT_X = 43;  // = 25 + 18 (worst case 4+4+2+4+4)
static constexpr int RIGHT_MARGIN = 1;   // pixeles libres al borde derecho
static constexpr int COLON_W = 2;        // avance horizontal del ":" (1px glifo + 1px gap)
static constexpr int COLON_Y_OFFSET = 0;  // ":" alineado con HH/MM

// RGB888 -> RGB565 (5 bits R, 6 bits G, 5 bits B).
#define RGB565(r, g, b) ((uint16_t)( (((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3) ))

// Per-row animation state. Cuando una fila cambia de icono se reinicia con phase
// = rowIdx % len(frames) para desincronizar filas con el mismo icono.
struct RowAnimState {
    int8_t iconType;       // ultimo IconType asignado (-1 si nada)
    int frameIdx;
    uint32_t nextChangeMs;
};
static RowAnimState animState[4] = {
    {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}
};

// Preview state: cuando activo, la fila 0 muestra g_previewFrames en vez del
// icono derivado de la meteo. Se setea desde la UI (POST /api/icons/preview)
// con los frames del icono que el usuario esta editando — asi puede ver en
// el panel real lo que hace sin tener que guardar.
static std::vector<Icons::Frame> g_previewFrames;
static RowAnimState g_previewState = {-1, 0, 0};
static bool g_previewActive = false;
static uint32_t g_previewExpireMs = 0;

void setIconPreview(const std::vector<Icons::Frame>& frames, uint32_t duration_ms) {
    g_previewActive = false;          // pausa lecturas mientras swap
    g_previewFrames = frames;          // copia
    g_previewState = {-1, 0, 0};
    g_previewExpireMs = duration_ms ? (millis() + duration_ms) : 0;
    g_previewActive = !g_previewFrames.empty();
}
void clearIconPreview() { g_previewActive = false; }
bool isIconPreviewActive() { return g_previewActive; }

static const Icons::Frame* tickPreviewAnim() {
    if (g_previewFrames.empty()) return nullptr;
    uint32_t now = millis();
    int n = (int)g_previewFrames.size();
    if (g_previewState.iconType < 0) {
        g_previewState.iconType = 1;     // marker "ya inicializado"
        g_previewState.frameIdx = 0;
        g_previewState.nextChangeMs = now + g_previewFrames[0].ms;
    } else if (n > 1 && (int32_t)(now - g_previewState.nextChangeMs) >= 0) {
        g_previewState.frameIdx = (g_previewState.frameIdx + 1) % n;
        g_previewState.nextChangeMs = now + g_previewFrames[g_previewState.frameIdx].ms;
    }
    return &g_previewFrames[g_previewState.frameIdx];
}

static const Icons::IconDef* iconDefForType(IconType::Value v) {
    if (v < 0 || (int)v >= Icons::NUM_ICONS) return nullptr;
    return &Icons::icons[(int)v];
}

// Avanza el frame si toca y devuelve el frame actual.
static const Icons::Frame* tickRowAnim(int rowIdx, IconType::Value v) {
    RowAnimState& st = animState[rowIdx];
    const Icons::IconDef* def = iconDefForType(v);
    if (!def || def->frames.empty()) {
        st.iconType = -1;
        return nullptr;
    }
    uint32_t now = millis();
    int n = (int)def->frames.size();
    if (st.iconType != (int8_t)v) {
        // Icono cambio (o se asigna por primera vez). Phase desync.
        st.iconType = (int8_t)v;
        st.frameIdx = rowIdx % n;
        st.nextChangeMs = now + def->frames[st.frameIdx].ms;
    } else if (n > 1 && (int32_t)(now - st.nextChangeMs) >= 0) {
        st.frameIdx = (st.frameIdx + 1) % n;
        st.nextChangeMs = now + def->frames[st.frameIdx].ms;
    }
    return &def->frames[st.frameIdx];
}

// Color por temperatura: gradiente continuo azul -> blanco calido -> rojo.
// Replica la rampa de CircuitPython para igualar percepcion entre los dos
// proyectos.
static int lerp(int a, int b, float t) { return (int)(a + (b - a) * t); }

static uint16_t tempColor(int t) {
    int r, g, b;
    if (t <= 0) {
        r = 60; g = 113; b = 247;
    } else if (t <= 10) {
        float k = t / 10.0f;
        r = lerp(60, 172, k); g = lerp(113, 199, k); b = lerp(247, 203, k);
    } else if (t <= 20) {
        float k = (t - 10) / 10.0f;
        r = lerp(172, 247, k); g = lerp(199, 145, k); b = lerp(203, 128, k);
    } else if (t <= 30) {
        float k = (t - 20) / 10.0f;
        r = 247; g = lerp(145, 76, k); b = lerp(128, 41, k);
    } else if (t <= 40) {
        float k = (t - 30) / 10.0f;
        r = 247; g = lerp(76, 0, k); b = lerp(41, 0, k);
    } else {
        r = 247; g = 0; b = 0;
    }
    return RGB565(r, g, b);
}

void begin() {
    // Si el panel cablea G/B intercambiados, cambia rgb_order a "RBG" en config.
    bool swapGB = Config::cfg.rgbOrder == "RBG";
    int8_t r1 = PIN_R1_GPIO;
    int8_t g1 = swapGB ? PIN_B1_GPIO : PIN_G1_GPIO;
    int8_t b1 = swapGB ? PIN_G1_GPIO : PIN_B1_GPIO;
    int8_t r2 = PIN_R2_GPIO;
    int8_t g2 = swapGB ? PIN_B2_GPIO : PIN_G2_GPIO;
    int8_t b2 = swapGB ? PIN_G2_GPIO : PIN_B2_GPIO;
    Serial.printf("[display] rgb_order=%s (swap_gb=%d)\n",
                  Config::cfg.rgbOrder.c_str(), (int)swapGB);
    HUB75_I2S_CFG::i2s_pins pins = {
        r1, g1, b1, r2, g2, b2,
        PIN_A, PIN_B, PIN_C, PIN_D, PIN_E,
        PIN_LAT, PIN_OE, PIN_CLK
    };
    HUB75_I2S_CFG cfg(WIDTH, HEIGHT, /*chain*/1, pins);
    cfg.clkphase = false;
    cfg.gpio.e = -1;
    cfg.double_buff = true;     // anti-flicker: render en back buffer + flip

    dma = new MatrixPanel_I2S_DMA(cfg);
    if (!dma->begin()) {
        Serial.println("[display] DMA begin failed");
        return;
    }
    dma->setBrightness8(128);   // 50% por defecto
    dma->setFont(&TomThumb);
    dma->setTextWrap(false);
    dma->clearScreen();
    dma->flipDMABuffer();
    dma->clearScreen();
    Serial.println("[display] HUB75 DMA up (double-buffered)");
}

void setBrightness(uint8_t v) {
    if (dma) dma->setBrightness8(v);
}

void clear() {
    if (dma) dma->clearScreen();
}

uint16_t rgb888to565(uint32_t rgb) {
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void drawFrame(int x, int y, const Icons::Frame& f) {
    for (int yy = 0; yy < Icons::ICON_H; yy++) {
        for (int xx = 0; xx < Icons::ICON_W; xx++) {
            uint8_t idx = f.px[yy][xx];
            if (idx == 0) continue;   // 0 = transparente
            dma->drawPixel(x + xx, y + yy, Icons::paletteAs565(idx));
        }
    }
}

static void drawDivider(int y, uint16_t color) {
    for (int x = 0; x < WIDTH; x++) dma->drawPixel(x, y, color);
}

// Mide el ancho de un string en la fuente activa (tom-thumb).
static int textWidth(const char* s) {
    int16_t x1, y1; uint16_t w, h;
    dma->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    return (int)w;
}

// Avance horizontal de un digito incluyendo 1px de gap. tom-thumb tiene "1" de
// 2px y resto 3px; con avance variable evitamos el "hueco" que dejaba el slot
// fijo monospace cuando aparecia un 1.
static int digitAdvance(char d) { return (d == '1') ? 3 : 4; }

static int drawDigit(char d, int x, int y) {
    char c[2] = {d, 0};
    dma->setCursor(x, y);
    dma->print(c);
    return x + digitAdvance(d);
}

// Dibuja "DD" empezando en x; devuelve la x posterior al ultimo digito.
static int drawTwoDigits(const char* s, int x, int y) {
    x = drawDigit(s[0], x, y);
    return drawDigit(s[1], x, y);
}

void renderRows(const Row rows[4]) {
    if (!dma) return;
    dma->clearScreen();

    uint16_t divCol = dma->color565(0x33, 0x33, 0x33);
    for (int i = 0; i < 3; i++) drawDivider(DIV_YS[i], divCol);

    // Posicion fija del icono. Usamos worst-case "-9" + simbolo de grado (2x2)
    // con 1px de gap, asi el icono no salta entre 1 y 2 digitos de temperatura.
    constexpr int DEG_W = 2;            // ancho del simbolo grado (2x2 filled)
    constexpr int DEG_GAP = 1;          // gap entre digitos y grado
    constexpr int ICON_GAP = 3;         // gap entre icono y temperatura
    constexpr int ICON_W = 5;
    int refDigitsW = textWidth("-9");
    int refTempTotal = refDigitsW + DEG_GAP + DEG_W;
    int rightEdge = WIDTH - RIGHT_MARGIN;
    int iconX = rightEdge - refTempTotal - ICON_GAP - ICON_W;

    for (int i = 0; i < 4; i++) {
        const Row& r = rows[i];
        int y = ROW_YS[i];

        // Nombre a la izquierda.
        dma->setTextColor(r.color);
        dma->setCursor(NAME_X, y);
        dma->print(r.name);

        if (r.hasTime) {
            char hh[3], mm[3];
            snprintf(hh, sizeof(hh), "%02u", r.hour);
            snprintf(mm, sizeof(mm), "%02u", r.minute);
            int blockW = digitAdvance(hh[0]) + digitAdvance(hh[1]) + COLON_W
                       + digitAdvance(mm[0]) + digitAdvance(mm[1]);
            int xStart = TIME_RIGHT_X - blockW;
            dma->setTextColor(r.color);
            int xCur = drawTwoDigits(hh, xStart, y);
            if (r.showColon) {
                dma->setCursor(xCur, y + COLON_Y_OFFSET);
                dma->print(":");
            }
            drawTwoDigits(mm, xCur + COLON_W, y);
        }

        if (r.hasWeather) {
            // Icono fijo (no se desplaza con la temp), animado segun frames.
            // Fila 0 puede tener preview override (icono en edicion).
            const Icons::Frame* f = nullptr;
            if (i == 0 && g_previewActive) {
                if (g_previewExpireMs && (int32_t)(millis() - g_previewExpireMs) >= 0) {
                    g_previewActive = false;
                    f = tickRowAnim(i, r.icon);
                } else {
                    f = tickPreviewAnim();
                }
            } else {
                f = tickRowAnim(i, r.icon);
            }
            if (f) drawFrame(iconX, y + ICON_Y_OFFSET, *f);

            // Temperatura right-aligned. El "°" lo dibujamos a mano (2x2 al top
            // de la linea) porque TomThumb GFX no incluye glifo para 0xB0.
            char tnum[6];
            snprintf(tnum, sizeof(tnum), "%d", r.tempC);
            int numW = textWidth(tnum);
            int totalW = numW + DEG_GAP + DEG_W;
            int xStart = rightEdge - totalW;
            uint16_t tcol = tempColor(r.tempC);
            dma->setTextColor(tcol);
            dma->setCursor(xStart, y);
            dma->print(tnum);
            int degX = xStart + numW + DEG_GAP;
            int degTopY = y - 5;
            dma->drawPixel(degX,     degTopY,     tcol);
            dma->drawPixel(degX + 1, degTopY,     tcol);
            dma->drawPixel(degX,     degTopY + 1, tcol);
            dma->drawPixel(degX + 1, degTopY + 1, tcol);
        }
    }
    dma->flipDMABuffer();   // intercambia front/back, anti-flicker
}

}  // namespace Display
