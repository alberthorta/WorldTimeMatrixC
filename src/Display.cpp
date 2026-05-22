#include "Display.h"

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Fonts/TomThumb.h>
#include <Fonts/FreeSans12pt7b.h>
#include <LittleFS.h>
#include <math.h>

#include "Config.h"
#include "Icons.h"
#include "MoonPhase.h"

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

// --- Crossfade real al cambiar HH:MM o temp ---
// Durante la transicion (FADE_MS), rasterizamos OLD y NEW en dos canvases
// 1-bit y blittmos al panel con alpha por pixel:
//     a_old = inOld ? (1-p) : 0
//     a_new = inNew ? p     : 0
//     final_alpha = a_old + a_new   (clamp 1.0)
// Asi pixeles compartidos por old y new se quedan a alpha=1.0 (no parpadean
// durante el fade), unicos hacen ramp suave 0→1 o 1→0.
static const uint32_t FADE_MS = 300;

struct FieldTrans {
    int8_t   oldA = 0;       // hour para time, tempC para temp
    int8_t   oldB = 0;       // minute para time, sin uso para temp
    bool     oldHas = false; // hasTime/hasWeather al inicio del fade
    uint32_t startMs = 0;
};
struct RowTrans {
    int8_t   prevHour = 0, prevMinute = 0, prevTempC = 0;
    bool     prevHasTime = false, prevHasWeather = false;
    bool     inited = false;     // primer render no dispara transicion
    FieldTrans timeTrans;
    FieldTrans tempTrans;
};
static RowTrans rowTrans[4];

// Canvases reutilizables para crossfade. Tamano max: HH:MM = "00:00" worst
// case ~18 px → canvas 22 ancho cubre time y temp con margen. Alocado una
// sola vez en begin(); reutilizado entre filas y entre time/temp dentro
// de cada render.
static const int FADE_CANVAS_W = 22;
static const int FADE_CANVAS_H = 8;
static GFXcanvas1* g_canvasOld = nullptr;
static GFXcanvas1* g_canvasNew = nullptr;

// Canvases adicionales para crossfade del HH:MM en los modos FOCUS (fuente
// FreeSans12pt7b, ~56x18) y CLAUDE (TomThumb, cabe en 22x7 igual que el
// existente, pero usamos par dedicado para no pisar el de renderRows si en
// el futuro se mezclan).
static const int FOCUS_FADE_CANVAS_W = 56;
static const int FOCUS_FADE_CANVAS_H = 20;
static GFXcanvas1* g_focusCanvasOld = nullptr;
static GFXcanvas1* g_focusCanvasNew = nullptr;
static const int CLAUDE_FADE_CANVAS_W = 22;
static const int CLAUDE_FADE_CANVAS_H = 7;
static GFXcanvas1* g_claudeCanvasOld = nullptr;
static GFXcanvas1* g_claudeCanvasNew = nullptr;

struct HHMMTransState {
    bool inited = false;
    int8_t prevHour = 0;
    int8_t prevMinute = 0;
    uint32_t startMs = 0;
};
static HHMMTransState s_focusHHMMTrans;
static HHMMTransState s_claudeHHMMTrans;

// Escala un color 565 por alpha [0..1] decodificando a 888, multiplicando por
// canal y reencodificando. Usado por el fade del colon, las transiciones de
// HH:MM/temp y el º cuando hay alpha.
static uint16_t scale565(uint16_t c, float alpha) {
    if (alpha >= 0.999f) return c;
    if (alpha <= 0.001f) return 0;
    uint8_t R5 = (c >> 11) & 0x1F;
    uint8_t G6 = (c >> 5)  & 0x3F;
    uint8_t B5 =  c        & 0x1F;
    uint8_t R = (R5 << 3) | (R5 >> 2);
    uint8_t G = (G6 << 2) | (G6 >> 4);
    uint8_t B = (B5 << 3) | (B5 >> 2);
    R = (uint8_t)((float)R * alpha + 0.5f);
    G = (uint8_t)((float)G * alpha + 0.5f);
    B = (uint8_t)((float)B * alpha + 0.5f);
    return rgb888to565(((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B);
}

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

// Para MOON y PARTLY_NIGHT, redirige a la variante de fase actual (MOON_NEW,
// MOON_WAXING, MOON_FULL, MOON_WANING — y equivalentes _NIGHT). Si la variante
// no esta definida o no hay NTP sync todavia, cae al icono base.
static const Icons::IconDef* iconDefForType(IconType::Value v) {
    if (v < 0 || (int)v >= Icons::NUM_ICONS) return nullptr;
    if (v == IconType::MOON || v == IconType::PARTLY_NIGHT) {
        time_t now = time(nullptr);
        // Sanity check: NTP no sincronizado todavia (epoch < 2023-11) -> fallback.
        if (now > 1700000000) {
            double age = MoonPhase::ageDays(now);
            MoonPhase::Phase p = MoonPhase::bucketize(age);
            const char* base = (v == IconType::MOON) ? "MOON" : "PARTLY_NIGHT";
            char name[32];
            snprintf(name, sizeof(name), "%s%s", base, MoonPhase::suffix(p));
            const Icons::IconDef* variant = Icons::find(name);
            if (variant && !variant->frames.empty()) return variant;
        }
    }
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
    // Clamp: la fase lunar puede cambiar el `def` (MOON_WAXING -> MOON_FULL)
    // sin que st.iconType cambie, y si el usuario edito frames a longitudes
    // distintas el frameIdx puede haber quedado fuera de rango.
    if (st.frameIdx >= n) {
        st.frameIdx = 0;
        st.nextChangeMs = now + def->frames[0].ms;
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
    g_canvasOld = new GFXcanvas1(FADE_CANVAS_W, FADE_CANVAS_H);
    g_canvasNew = new GFXcanvas1(FADE_CANVAS_W, FADE_CANVAS_H);
    g_focusCanvasOld  = new GFXcanvas1(FOCUS_FADE_CANVAS_W, FOCUS_FADE_CANVAS_H);
    g_focusCanvasNew  = new GFXcanvas1(FOCUS_FADE_CANVAS_W, FOCUS_FADE_CANVAS_H);
    g_claudeCanvasOld = new GFXcanvas1(CLAUDE_FADE_CANVAS_W, CLAUDE_FADE_CANVAS_H);
    g_claudeCanvasNew = new GFXcanvas1(CLAUDE_FADE_CANVAS_W, CLAUDE_FADE_CANVAS_H);
}

void setBrightness(uint8_t v) {
    if (dma) dma->setBrightness8(v);
}

void clear() {
    if (dma) dma->clearScreen();
}

void fillScreen(uint16_t color565) {
    if (!dma) return;
    dma->fillScreen(color565);
    dma->flipDMABuffer();
}

// Ripple state: hasta 2 ripples simultaneos (uno por boton). El draw real se
// hace al final de renderRows, asi se aplica como overlay encima del reloj.
struct RippleState {
    bool active;
    int16_t cx, cy;
    uint32_t startMs;
};
static RippleState g_ripples[3] = {{false,0,0,0}, {false,0,0,0}, {false,0,0,0}};
static constexpr uint32_t RIPPLE_DURATION_MS = 700;
static constexpr float RIPPLE_MAX_RADIUS = 40.0f;

void triggerRipple(int slot, int16_t cx, int16_t cy) {
    if (slot < 0 || slot >= 3) return;
    g_ripples[slot].active = true;
    g_ripples[slot].cx = cx;
    g_ripples[slot].cy = cy;
    g_ripples[slot].startMs = millis();
}

static uint16_t blendColor(uint16_t cOld, float aOld, uint16_t cNew, float aNew);   // fwd
static void drawCommonBottomRow(const Row& wRow, float secondOfMinuteF);            // fwd

// Dibuja una barra de segundos "smooth" con la cabeza renderizada sub-pixel:
// los pixeles ya recorridos se pintan en color dim; la cabeza se reparte
// entre dos pixeles adyacentes con alpha proporcional a la fraccion de
// segundo, dando sensacion de movimiento continuo a la velocidad del render.
static void drawSecondsBarSmooth(int x0, int y0, int barW, int height,
                                 float secondOfMinuteF) {
    if (secondOfMinuteF < 0.0f || secondOfMinuteF >= 60.0f) return;
    float barEndF = secondOfMinuteF * (float)barW / 60.0f;
    int barEndInt = (int)floorf(barEndF);
    float frac = barEndF - (float)barEndInt;
    uint16_t dimBar  = rgb888to565(0x303030);
    uint16_t headBar = rgb888to565(0xC0C0C0);
    for (int x = 0; x < barEndInt; x++) {
        for (int dy = 0; dy < height; dy++) {
            dma->drawPixel(x0 + x, y0 + dy, dimBar);
        }
    }
    if (barEndInt >= 0 && barEndInt < barW) {
        // Interpola entre head y dim segun la fraccion: cuando frac=0 el
        // pixel es head pleno, cuando frac=1 es practicamente dim (porque
        // la cabeza ha "pasado" al siguiente).
        uint16_t c = blendColor(headBar, 1.0f - frac, dimBar, frac);
        for (int dy = 0; dy < height; dy++) {
            dma->drawPixel(x0 + barEndInt, y0 + dy, c);
        }
    }
    if (barEndInt + 1 >= 0 && barEndInt + 1 < barW) {
        // Pixel donde la cabeza esta apareciendo: alpha=frac sobre head.
        uint16_t c = scale565(headBar, frac);
        for (int dy = 0; dy < height; dy++) {
            dma->drawPixel(x0 + barEndInt + 1, y0 + dy, c);
        }
    }
}

// Calcula el alpha del ":" entre HH y MM segun el segundo actual y el flag
// colonBlink del Config. Misma formula que la usada en renderRows: parpadeo
// 1Hz con fade de 200ms al cambiar. Si colonBlink esta off devuelve 1.0.
static float computeColonAlpha(float secondOfMinuteF) {
    if (!Config::cfg.colonBlink || secondOfMinuteF < 0.0f) return 1.0f;
    int intSec = (int)floorf(secondOfMinuteF);
    float frac = secondOfMinuteF - (float)intSec;
    float target = ((intSec % 2) == 0) ? 1.0f : 0.0f;
    float prev   = 1.0f - target;
    const float FADE_SEC = 4.0f / 20.0f;       // 200 ms a 20 fps
    if (frac < FADE_SEC) {
        float p = frac / FADE_SEC;
        return prev * (1.0f - p) + target * p;
    }
    return target;
}

// Overlay del brillo (label + barra + %). Se dispara desde main al pulsar
// los botones izq/der. Cada trigger renueva el timer; cuando expira, vuelve
// el render del reloj normal.
static uint32_t g_brightnessOverlayStartMs = 0;
static float    g_brightnessOverlayValue = 0.0f;
static constexpr uint32_t BRIGHTNESS_OVERLAY_DURATION_MS = 1500;

void triggerBrightnessOverlay(float value) {
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    g_brightnessOverlayValue = value;
    g_brightnessOverlayStartMs = millis();
}

static int textWidth(const char* s);   // forward
static void drawBrightnessOverlay() {
    if (!dma || g_brightnessOverlayStartMs == 0) return;
    uint32_t age = millis() - g_brightnessOverlayStartMs;
    if (age >= BRIGHTNESS_OVERLAY_DURATION_MS) {
        g_brightnessOverlayStartMs = 0;
        return;
    }
    uint16_t white = rgb888to565(0xFFFFFF);
    uint16_t black = 0;

    // Caja centrada que cubre casi toda la pantalla (60x26, deja 2px de
    // margen lateral y 3 arriba/abajo). Fondo negro + borde blanco para
    // separarse del reloj que tiene debajo.
    const int BX = 2, BY = 3, BW = 60, BH = 26;
    dma->fillRect(BX, BY, BW, BH, black);
    dma->drawRect(BX, BY, BW, BH, white);

    dma->setFont(&TomThumb);
    dma->setTextColor(white);

    // Label "BRIGHTNESS" centrado, baseline a 5px del top de la caja.
    const char* label = "BRIGHTNESS";
    int wl = textWidth(label);
    dma->setCursor((WIDTH - wl) / 2, BY + 6);
    dma->print(label);

    // Barra de progreso: borde + relleno proporcional al brightness.
    const int BAR_X = BX + 4;
    const int BAR_Y = BY + 11;
    const int BAR_W = BW - 8;
    const int BAR_H = 4;
    dma->drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, white);
    int fillW = (int)(g_brightnessOverlayValue * (float)(BAR_W - 2) + 0.5f);
    if (fillW > BAR_W - 2) fillW = BAR_W - 2;
    if (fillW > 0) dma->fillRect(BAR_X + 1, BAR_Y + 1, fillW, BAR_H - 2, white);

    // Porcentaje en la fila inferior.
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%",
             (int)(g_brightnessOverlayValue * 100.0f + 0.5f));
    int wp = textWidth(pct);
    dma->setCursor((WIDTH - wp) / 2, BY + 23);
    dma->print(pct);
}

// Dibuja los ripples activos. Cada uno es un anillo (2px grosor) que crece de
// radio 1 a RIPPLE_MAX_RADIUS y se desvanece linealmente. Llamado desde
// renderRows antes del flip.
static void drawActiveRipples() {
    if (!dma) return;
    uint32_t now = millis();
    for (int i = 0; i < 3; i++) {
        if (!g_ripples[i].active) continue;
        uint32_t age = now - g_ripples[i].startMs;
        if (age >= RIPPLE_DURATION_MS) {
            g_ripples[i].active = false;
            continue;
        }
        float t = (float)age / (float)RIPPLE_DURATION_MS;
        float radius = t * RIPPLE_MAX_RADIUS;
        float alpha  = 1.0f - t;
        if (radius < 1.0f) continue;
        uint16_t color = scale565(0xFFFF, alpha);
        int r1 = (int)radius;
        dma->drawCircle(g_ripples[i].cx, g_ripples[i].cy, r1, color);
        // Anillo interior 1px mas pequeño con un poco menos de intensidad,
        // para dar grosor a la onda y que no se vea pixelado en saltos.
        if (r1 >= 2) {
            uint16_t color2 = scale565(0xFFFF, alpha * 0.6f);
            dma->drawCircle(g_ripples[i].cx, g_ripples[i].cy, r1 - 1, color2);
        }
    }
}

static int textWidth(const char* s);   // forward, def mas abajo
void drawSplash(const char* const lines[], int nLines) {
    if (!dma) return;
    dma->clearScreen();
    dma->setFont(&TomThumb);
    uint16_t white = rgb888to565(0xFFFFFF);
    dma->setTextColor(white);
    // Cuenta lineas no vacias para centrado vertical real (ignora "" trailing).
    int realLines = 0;
    for (int i = 0; i < nLines && i < 4; i++) {
        if (lines[i] && *lines[i]) realLines++;
    }
    if (realLines == 0) { dma->flipDMABuffer(); return; }
    // Cada linea ocupa 8 px de alto (mismo paso que ROW_YS). Baseline de la
    // primera linea = padding superior + 6 (altura de la fuente desde top).
    const int LINE_H = 8;
    int startBaseline = (HEIGHT - LINE_H * realLines) / 2 + 6;
    int row = 0;
    for (int i = 0; i < nLines && i < 4; i++) {
        const char* s = lines[i];
        if (!s || !*s) continue;
        int w = textWidth(s);
        int x = (WIDTH - w) / 2;
        if (x < 0) x = 0;
        dma->setCursor(x, startBaseline + LINE_H * row);
        dma->print(s);
        row++;
    }
    dma->flipDMABuffer();
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
static int textWidth_GFX(Adafruit_GFX& g, const char* s) {
    int16_t x1, y1; uint16_t w, h;
    g.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    return (int)w;
}

// Compone color final cuando dos fuentes contribuyen (crossfade): suma per-canal
// con clamp. Si ambos son el mismo color, equivale a scale565(c, aOld+aNew).
static uint16_t blendColor(uint16_t cOld, float aOld, uint16_t cNew, float aNew) {
    if (aOld <= 0.001f && aNew <= 0.001f) return 0;
    auto decode = [](uint16_t c, uint8_t& R, uint8_t& G, uint8_t& B) {
        uint8_t R5 = (c >> 11) & 0x1F;
        uint8_t G6 = (c >> 5)  & 0x3F;
        uint8_t B5 =  c        & 0x1F;
        R = (R5 << 3) | (R5 >> 2);
        G = (G6 << 2) | (G6 >> 4);
        B = (B5 << 3) | (B5 >> 2);
    };
    uint8_t Ro=0, Go=0, Bo=0, Rn=0, Gn=0, Bn=0;
    decode(cOld, Ro, Go, Bo);
    decode(cNew, Rn, Gn, Bn);
    int R = (int)((float)Ro * aOld + (float)Rn * aNew + 0.5f); if (R > 255) R = 255;
    int G = (int)((float)Go * aOld + (float)Gn * aNew + 0.5f); if (G > 255) G = 255;
    int B = (int)((float)Bo * aOld + (float)Bn * aNew + 0.5f); if (B > 255) B = 255;
    return rgb888to565(((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B);
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
static int drawDigit_GFX(Adafruit_GFX& g, char d, int x, int y) {
    char c[2] = {d, 0};
    g.setCursor(x, y);
    g.print(c);
    return x + digitAdvance(d);
}
static int drawDigits_GFX(Adafruit_GFX& g, const char* s, int x, int y) {
    while (*s) { x = drawDigit_GFX(g, *s++, x, y); }
    return x;
}

// Rasteriza HH:MM (con colon) en canvas 1-bit, right-aligned al borde derecho.
// Si !hasTime, deja el canvas vacio.
static void rasterizeTime(GFXcanvas1& cnv, int hour, int minute, bool hasTime) {
    cnv.fillScreen(0);
    if (!hasTime) return;
    cnv.setFont(&TomThumb);
    cnv.setTextColor(1);
    char hh[3], mm[3];
    if (Config::cfg.hourLeadingZero || hour >= 10) snprintf(hh, sizeof(hh), "%02u", hour);
    else snprintf(hh, sizeof(hh), "%u", hour);
    snprintf(mm, sizeof(mm), "%02u", minute);
    int blockW = COLON_W;
    for (const char* p = hh; *p; p++) blockW += digitAdvance(*p);
    for (const char* p = mm; *p; p++) blockW += digitAdvance(*p);
    int xStart = cnv.width() - blockW;
    int yBase = 6;       // baseline dentro del canvas (height 8)
    int xCur = drawDigits_GFX(cnv, hh, xStart, yBase);
    drawDigit_GFX(cnv, ':', xCur, yBase + COLON_Y_OFFSET);
    drawDigits_GFX(cnv, mm, xCur + COLON_W, yBase);
}

// Rasteriza temp (incluyendo el º manual 2x2) en canvas 1-bit, right-aligned.
static void rasterizeTemp(GFXcanvas1& cnv, int tempC, bool hasWeather) {
    cnv.fillScreen(0);
    if (!hasWeather) return;
    cnv.setFont(&TomThumb);
    cnv.setTextColor(1);
    char tnum[6];
    snprintf(tnum, sizeof(tnum), "%d", tempC);
    int numW = textWidth_GFX(cnv, tnum);
    const int DEG_W = 2, DEG_GAP = 1;
    int totalW = numW + DEG_GAP + DEG_W;
    int xStart = cnv.width() - totalW;
    int yBase = 6;
    cnv.setCursor(xStart, yBase);
    cnv.print(tnum);
    int degX = xStart + numW + DEG_GAP;
    int degTopY = yBase - 5;
    cnv.drawPixel(degX,     degTopY,     1);
    cnv.drawPixel(degX + 1, degTopY,     1);
    cnv.drawPixel(degX,     degTopY + 1, 1);
    cnv.drawPixel(degX + 1, degTopY + 1, 1);
}

// Dibuja una secuencia de digitos empezando en x; devuelve la x posterior
// al ultimo digito. Soporta 1 o 2 chars (e.g., "07" o "7") para que funcione
// con/sin cero a la izquierda de la hora.
static int drawDigits(const char* s, int x, int y) {
    while (*s) {
        x = drawDigit(*s, x, y);
        s++;
    }
    return x;
}

void renderRows(const Row rows[4], float secondOfMinuteF) {
    if (!dma) return;
    dma->clearScreen();

    // Barra vertical de segundos (modo BAR): columna full-height detras del
    // contenido. Render por overlap pixel-caja con la barra continua centrada
    // en pos_f y ancho secondsBarWidth, dando 2 modos:
    //   - Normal: solo la barra (con flancos antialiased), wrap toroidal al
    //     pasar por el minuto.
    //   - Progress: rellena [0, pos_f + W/2] (zona ya recorrida) con flanco
    //     suavizado a la derecha; al cruzar minuto resetea (no wrap).
    if (Config::cfg.secondsIndicator == Config::SecondsIndicator::BAR &&
        secondOfMinuteF >= 0.0f && secondOfMinuteF < 60.0f) {
        bool progress = Config::cfg.secondsBarProgress;
        float pos_f = secondOfMinuteF * (float)WIDTH / 60.0f;
        if (!progress) pos_f = fmodf(pos_f, (float)WIDTH);   // toroidal solo en modo normal
        float halfW = (float)Config::cfg.secondsBarWidth * 0.5f;
        float left  = pos_f - halfW;
        float right = pos_f + halfW;
        // En progress mode la "barra" es realmente el rango [0, right] —
        // funciona via la misma formula con left forzado a 0.
        if (progress) left = 0.0f;
        uint32_t base = Config::cfg.secondsBarColor;
        uint8_t bR = (base >> 16) & 0xFF;
        uint8_t bG = (base >> 8)  & 0xFF;
        uint8_t bB =  base        & 0xFF;
        auto paintCol = [&](int xw, float w) {
            if (w <= 0.0f) return;
            if (w > 1.0f) w = 1.0f;
            uint8_t R = (uint8_t)((float)bR * w + 0.5f);
            uint8_t G = (uint8_t)((float)bG * w + 0.5f);
            uint8_t B = (uint8_t)((float)bB * w + 0.5f);
            uint16_t c = rgb888to565(((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B);
            if (!c) return;
            for (int y = 0; y < HEIGHT; y++) dma->drawPixel(xw, y, c);
        };
        // Recorremos solo el rango necesario. En progress, [0, right]; en
        // normal, [floor(left)-1, ceil(right)+1] con wrap.
        int x0 = progress ? 0 : ((int)floorf(left) - 1);
        int x1 = (int)ceilf(right) + 1;
        for (int x = x0; x <= x1; x++) {
            float pxL = (float)x - 0.5f;
            float pxR = (float)x + 0.5f;
            float ovL = fmaxf(left,  pxL);
            float ovR = fminf(right, pxR);
            float w = ovR - ovL;
            if (w <= 0.0f) continue;
            int xw;
            if (progress) {
                if (x < 0 || x >= WIDTH) continue;     // sin wrap
                xw = x;
            } else {
                xw = ((x % WIDTH) + WIDTH) % WIDTH;
            }
            paintCol(xw, w);
        }
    }

    uint16_t divCol = dma->color565(0x33, 0x33, 0x33);
    for (int i = 0; i < 3; i++) drawDivider(DIV_YS[i], divCol);

    // Posicion fija del icono. Usamos worst-case "-9" + simbolo de grado (2x2)
    // con 1px de gap, asi el icono no salta entre 1 y 2 digitos de temperatura.
    constexpr int DEG_W = 2;            // ancho del simbolo grado (2x2 filled)
    constexpr int DEG_GAP = 1;          // gap entre digitos y grado
    constexpr int ICON_W = 5;
    // Cuando el indicador de tendencia esta activo, reservamos 3 cols a la
    // derecha de la temp (1 px margen + 2 px indicador) y desplazamos hora
    // (-1), icono (-2) y temp (-3) para acomodarlo. iconGap pasa de 3 → 2
    // para conseguir el shift -2 del icono mientras la temp se va -3.
    bool trendShift = Config::cfg.forecastIndicatorEnabled;
    int iconGap = trendShift ? 2 : 3;
    int rightEdgeShift = trendShift ? 3 : 0;
    int hourRightX = TIME_RIGHT_X - (trendShift ? 1 : 0);
    int refDigitsW = textWidth("-9");
    int refTempTotal = refDigitsW + DEG_GAP + DEG_W;
    int rightEdge = WIDTH - RIGHT_MARGIN - rightEdgeShift;
    int iconX = rightEdge - refTempTotal - iconGap - ICON_W;

    uint32_t nowMs = millis();
    for (int i = 0; i < 4; i++) {
        const Row& r = rows[i];
        int y = ROW_YS[i];
        RowTrans& tr = rowTrans[i];

        // Detecta cambios respecto al render anterior y dispara transiciones.
        // Skip en el primer render para no fade-in desde 0 al boot.
        if (tr.inited) {
            if (tr.timeTrans.startMs == 0) {
                bool changed = (r.hasTime != tr.prevHasTime) ||
                               (r.hasTime && (r.hour != tr.prevHour || r.minute != tr.prevMinute));
                if (changed) {
                    tr.timeTrans.oldA = tr.prevHour;
                    tr.timeTrans.oldB = tr.prevMinute;
                    tr.timeTrans.oldHas = tr.prevHasTime;
                    tr.timeTrans.startMs = nowMs;
                }
            }
            if (tr.tempTrans.startMs == 0) {
                bool changed = (r.hasWeather != tr.prevHasWeather) ||
                               (r.hasWeather && r.tempC != tr.prevTempC);
                if (changed) {
                    tr.tempTrans.oldA = r.hasWeather ? tr.prevTempC : 0;
                    tr.tempTrans.oldHas = tr.prevHasWeather;
                    tr.tempTrans.startMs = nowMs;
                }
            }
        }
        tr.prevHour = r.hour;
        tr.prevMinute = r.minute;
        tr.prevTempC = r.tempC;
        tr.prevHasTime = r.hasTime;
        tr.prevHasWeather = r.hasWeather;
        tr.inited = true;

        bool timeFadeActive = (tr.timeTrans.startMs != 0) &&
                              (nowMs - tr.timeTrans.startMs < FADE_MS);
        bool tempFadeActive = (tr.tempTrans.startMs != 0) &&
                              (nowMs - tr.tempTrans.startMs < FADE_MS);
        float timeP = timeFadeActive
            ? (float)(nowMs - tr.timeTrans.startMs) / (float)FADE_MS : 1.0f;
        float tempP = tempFadeActive
            ? (float)(nowMs - tr.tempTrans.startMs) / (float)FADE_MS : 1.0f;
        if (tr.timeTrans.startMs && (nowMs - tr.timeTrans.startMs >= FADE_MS))
            tr.timeTrans.startMs = 0;
        if (tr.tempTrans.startMs && (nowMs - tr.tempTrans.startMs >= FADE_MS))
            tr.tempTrans.startMs = 0;

        // Nombre a la izquierda (sin fade — el nombre rara vez cambia).
        dma->setTextColor(r.color);
        dma->setCursor(NAME_X, y);
        dma->print(r.name);

        // HH:MM con crossfade activo → rasterizar OLD y NEW en canvases 1-bit
        // y blittear con alpha por pixel. Sin fade → render directo (mas
        // barato y compatible con el blink del colon).
        if (timeFadeActive && g_canvasOld && g_canvasNew) {
            rasterizeTime(*g_canvasOld, tr.timeTrans.oldA, tr.timeTrans.oldB, tr.timeTrans.oldHas);
            rasterizeTime(*g_canvasNew, r.hour, r.minute, r.hasTime);
            int CW = g_canvasOld->width(), CH = g_canvasOld->height();
            // hourRightX es la columna post-ultimo-pixel (donde caeria el
            // cursor al terminar de imprimir), no el ultimo pixel visible.
            // El canvas ocupa cols [0..CW-1] con el ultimo pixel dibujado en
            // CW-1, asi que mapeamos CW-1 → hourRightX-1 (panelX0=hourRightX-CW).
            int panelX0 = hourRightX - CW;
            int panelY0 = y - 6;                    // canvas baseline (y=6) → panel baseline (y)
            for (int yy = 0; yy < CH; yy++) {
                int py = panelY0 + yy;
                if (py < 0 || py >= HEIGHT) continue;
                for (int xx = 0; xx < CW; xx++) {
                    bool inA = g_canvasOld->getPixel(xx, yy);
                    bool inB = g_canvasNew->getPixel(xx, yy);
                    if (!inA && !inB) continue;
                    float aOld = inA ? (1.0f - timeP) : 0.0f;
                    float aNew = inB ? timeP : 0.0f;
                    float a = aOld + aNew;
                    if (a > 1.0f) a = 1.0f;
                    if (a < 0.005f) continue;
                    int px = panelX0 + xx;
                    if (px < 0 || px >= WIDTH) continue;
                    dma->drawPixel(px, py, scale565(r.color, a));
                }
            }
        } else if (r.hasTime) {
            char hh[3], mm[3];
            if (Config::cfg.hourLeadingZero || r.hour >= 10) {
                snprintf(hh, sizeof(hh), "%02u", r.hour);
            } else {
                snprintf(hh, sizeof(hh), "%u", r.hour);
            }
            snprintf(mm, sizeof(mm), "%02u", r.minute);
            int blockW = COLON_W;
            for (const char* p = hh; *p; p++) blockW += digitAdvance(*p);
            for (const char* p = mm; *p; p++) blockW += digitAdvance(*p);
            int xStart = hourRightX - blockW;
            dma->setTextColor(r.color);
            int xCur = drawDigits(hh, xStart, y);
            if (r.colonAlpha > 0.01f) {
                uint16_t colonColor = scale565(r.color, r.colonAlpha);
                dma->setTextColor(colonColor);
                dma->setCursor(xCur, y + COLON_Y_OFFSET);
                dma->print(":");
                dma->setTextColor(r.color);
            }
            drawDigits(mm, xCur + COLON_W, y);
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

            // Temperatura: con fade activo → canvas crossfade (cada lado puede
            // tener color distinto si tempColor() cambia de bucket). Sin fade
            // → render directo. El "°" se dibuja como pixeles en ambos paths.
            // Calculo de degX se preserva para el indicador de tendencia mas
            // abajo, que se ancla al simbolo de grado del estado nuevo.
            char tnum[6];
            snprintf(tnum, sizeof(tnum), "%d", r.tempC);
            int numW = textWidth(tnum);
            int totalW = numW + DEG_GAP + DEG_W;
            int xStart = rightEdge - totalW;
            int degX = xStart + numW + DEG_GAP;
            int degTopY = y - 5;
            uint16_t tcol = tempColor(r.tempC);
            if (tempFadeActive && g_canvasOld && g_canvasNew) {
                rasterizeTemp(*g_canvasOld, tr.tempTrans.oldA, tr.tempTrans.oldHas);
                rasterizeTemp(*g_canvasNew, r.tempC, r.hasWeather);
                uint16_t cOld = tempColor(tr.tempTrans.oldA);
                uint16_t cNew = tcol;
                int CW = g_canvasOld->width(), CH = g_canvasOld->height();
                int panelX0 = rightEdge - CW;       // off-by-one: ver comentario en time
                int panelY0 = y - 6;
                for (int yy = 0; yy < CH; yy++) {
                    int py = panelY0 + yy;
                    if (py < 0 || py >= HEIGHT) continue;
                    for (int xx = 0; xx < CW; xx++) {
                        bool inA = g_canvasOld->getPixel(xx, yy);
                        bool inB = g_canvasNew->getPixel(xx, yy);
                        if (!inA && !inB) continue;
                        float aOld = inA ? (1.0f - tempP) : 0.0f;
                        float aNew = inB ? tempP : 0.0f;
                        int px = panelX0 + xx;
                        if (px < 0 || px >= WIDTH) continue;
                        dma->drawPixel(px, py, blendColor(cOld, aOld, cNew, aNew));
                    }
                }
            } else {
                dma->setTextColor(tcol);
                dma->setCursor(xStart, y);
                dma->print(tnum);
                dma->drawPixel(degX,     degTopY,     tcol);
                dma->drawPixel(degX + 1, degTopY,     tcol);
                dma->drawPixel(degX,     degTopY + 1, tcol);
                dma->drawPixel(degX + 1, degTopY + 1, tcol);
            }
            // Indicador OM: 1 pixel #444 en la baseline del texto, alineado
            // con el simbolo de grado. Se enciende solo si la fila esta
            // usando Open-Meteo y la opcion esta activa en Config.
            if (r.omIndicator) {
                dma->drawPixel(degX + 1, y - 1, rgb888to565(0x444444));
            }
            // Indicador de tendencia: 2 px ancho a la derecha del º. Estados:
            //   STABLE  → "=" (cols × {centro-1, centro+1}) en color stable
            //   RISING  → bar 1..3 px hacia arriba, color rising
            //   FALLING → bar 1..3 px hacia abajo, color falling
            //   NONE    → no se dibuja
            // Colores configurables desde la UI.
            if (Config::cfg.forecastIndicatorEnabled &&
                r.trendState != Row::TrendState::TS_OFF) {
                int indL = degX + DEG_W + 1;     // 1 px margen tras el º (DEG_W=2)
                int indR = indL + 1;
                if (indR < WIDTH) {
                    int rowCenterY = ROW_YS[i] - 3;   // centro vertical aprox
                    if (r.trendState == Row::TrendState::TS_STABLE) {
                        uint16_t col = rgb888to565(Config::cfg.forecastColorStable);
                        int yA = rowCenterY - 1;
                        int yB = rowCenterY + 1;
                        if (yA >= 0)         { dma->drawPixel(indL, yA, col); dma->drawPixel(indR, yA, col); }
                        if (yB < HEIGHT)     { dma->drawPixel(indL, yB, col); dma->drawPixel(indR, yB, col); }
                    } else {
                        bool rising = (r.trendState == Row::TrendState::TS_RISING);
                        uint16_t col = rgb888to565(rising
                            ? Config::cfg.forecastColorRising
                            : Config::cfg.forecastColorFalling);
                        int mag = r.trendMagnitude;
                        if (mag > 3) mag = 3;
                        for (int n = 0; n < mag; n++) {
                            int dy = rising ? -n : n;
                            int py = rowCenterY + dy;
                            if (py >= 0 && py < HEIGHT) {
                                dma->drawPixel(indL, py, col);
                                dma->drawPixel(indR, py, col);
                            }
                        }
                    }
                }
            }
        }
    }
    // Barra de segundos: equivalente continuo del antiguo patron 3-px
    // (lateral=#333 / centro=#666 / lateral=#333) pero con renderizado
    // sub-pixel para movimiento suave a la velocidad de render.
    //
    // Modelo: tres "fuentes" con kernel triangular de ancho 1 (intensidad
    // ramp 0→1→0 en ±1 px) — una en pos_f-1 (lateral), una en pos_f
    // (centro), una en pos_f+1 (lateral). Para cada pixel entero del rango,
    // se suman las contribuciones de las tres fuentes ponderadas por la
    // distancia. Cuando pos_f cae en entero el resultado son 3 pixeles
    // exactos con los valores originales; cuando esta entre dos pixeles
    // el patron se difumina a 4 pixeles con pesos simetricos. Es una
    // convolucion del kernel discreto con la posicion sub-pixel.
    if (Config::cfg.secondsIndicator == Config::SecondsIndicator::MARKER &&
        secondOfMinuteF >= 0.0f && secondOfMinuteF < 60.0f) {
        // Sweep cíclico (toroidal): pos_f recorre [0, WIDTH) en 60s. Al pasar
        // del segundo 59→0, la barra se enrolla por el otro lado (los pixeles
        // del flanco que se salen por la derecha aparecen por la izquierda y
        // viceversa). Por eso usamos WIDTH como divisor (no WIDTH-1) — asi 0
        // y WIDTH-1 son toroidalmente adyacentes.
        float pos_f = fmodf(secondOfMinuteF * (float)WIDTH / 60.0f, (float)WIDTH);
        int y = HEIGHT - 1;
        const float MID  = 0x77;     // grayscale center  (antes 0x66)
        const float SIDE = 0x44;     // grayscale flanco  (antes 0x33)
        int x0 = (int)floorf(pos_f) - 1;
        int x1 = (int)ceilf(pos_f) + 1;
        for (int x = x0; x <= x1; x++) {
            // Contribucion = max(0, 1 - |dist|) por cada una de las 3 fuentes
            // del kernel. La distancia se calcula con x sin envolver para que
            // los flancos se midan respecto a sus fuentes reales; la x final
            // de pintado se envuelve a [0, WIDTH) para el wrap toroidal.
            float dC = (float)x - pos_f;
            float dL = (float)x - (pos_f - 1.0f);
            float dR = (float)x - (pos_f + 1.0f);
            float wC = 1.0f - fabsf(dC); if (wC < 0.0f) wC = 0.0f;
            float wL = 1.0f - fabsf(dL); if (wL < 0.0f) wL = 0.0f;
            float wR = 1.0f - fabsf(dR); if (wR < 0.0f) wR = 0.0f;
            float v = wC * MID + (wL + wR) * SIDE;
            int iv = (int)(v + 0.5f);
            if (iv <= 0) continue;
            if (iv > 0xFF) iv = 0xFF;
            int xw = x % WIDTH;
            if (xw < 0) xw += WIDTH;
            uint32_t rgb = ((uint32_t)iv << 16) | ((uint32_t)iv << 8) | (uint32_t)iv;
            dma->drawPixel(xw, y, rgb888to565(rgb));
        }
    }
    // Overlay del ripple (gota de los TTPs) antes del flip, para que se vea
    // encima del reloj sin parpadeo.
    drawActiveRipples();
    // Overlay del brillo: cubre la pantalla mientras esta activo, por lo que
    // va despues del ripple (un toque al subir/bajar brillo se ve sobre la
    // caja, no detras). Si no esta activo no hace nada.
    drawBrightnessOverlay();
    dma->flipDMABuffer();   // intercambia front/back, anti-flicker
}

// Renderiza HH:MM con dos efectos compartidos entre los modos FOCUS y CLAUDE:
//   1) Crossfade pixel-por-pixel al cambiar de minuto (300 ms), reusando dos
//      canvases 1-bit con el OLD y el NEW rasterizados con la misma baseline.
//   2) Colon blink: solo activo cuando no hay crossfade (durante el fade lo
//      ignoramos porque solo dura 300 ms y no compensa el coste).
// `dstCenterX` define el centro horizontal donde se alinea el HH:MM en el
// panel; `dstBaselineY` la baseline. `color` se aplica al texto entero (los
// 565 a alpha 0..1 se hacen con scale565).
static void renderHHMMWithFade(
    HHMMTransState& tr,
    GFXcanvas1* cOld, GFXcanvas1* cNew,
    const GFXfont* font,
    uint8_t hour, uint8_t minute, bool leadingZero,
    int dstCenterX, int dstBaselineY,
    uint16_t color, float colonAlpha) {

    uint32_t now = millis();
    if (!tr.inited) {
        tr.prevHour = (int8_t)hour;
        tr.prevMinute = (int8_t)minute;
        tr.inited = true;
    }
    bool changed = (tr.prevHour != (int8_t)hour) || (tr.prevMinute != (int8_t)minute);
    if (changed && tr.startMs == 0) tr.startMs = now;
    bool inFade = (tr.startMs != 0);
    uint32_t elapsed = inFade ? (now - tr.startMs) : 0;
    if (inFade && elapsed >= FADE_MS) {
        tr.prevHour = (int8_t)hour;
        tr.prevMinute = (int8_t)minute;
        tr.startMs = 0;
        inFade = false;
    }

    auto formatTime = [&](char* hh, size_t hhSz, char* mm, size_t mmSz,
                          char* full, size_t fullSz,
                          uint8_t h, uint8_t m) {
        bool lz = leadingZero || h >= 10;
        if (lz) snprintf(hh, hhSz, "%02u", h);
        else    snprintf(hh, hhSz, "%u",  h);
        snprintf(mm, mmSz, "%02u", m);
        snprintf(full, fullSz, "%s:%s", hh, mm);
    };

    if (!inFade) {
        // Render directo al panel con colon blink.
        char hh[4], mm[4], full[8];
        formatTime(hh, sizeof(hh), mm, sizeof(mm), full, sizeof(full), hour, minute);
        dma->setFont(font);
        dma->setTextSize(1);
        int16_t x1, y1; uint16_t w, h;
        dma->getTextBounds(full, 0, 0, &x1, &y1, &w, &h);
        int xStart = dstCenterX - (int)w / 2 - (int)x1;
        dma->setCursor(xStart, dstBaselineY);
        dma->setTextColor(color);
        dma->print(hh);
        dma->setTextColor(scale565(color, colonAlpha));
        dma->print(":");
        dma->setTextColor(color);
        dma->print(mm);
        return;
    }

    // En fade: rasterizar OLD y NEW en sus canvases con la misma baseline.
    // Cada canvas alinea el texto con su top-left interno en (1, 0) (1 px de
    // margen izq por seguridad). Guardamos el ancho real para centrar luego.
    auto rasterize = [&](GFXcanvas1& c, uint8_t h, uint8_t m) -> int {
        c.fillScreen(0);
        c.setFont(font);
        c.setTextSize(1);
        c.setTextColor(1);
        char hh[4], mm[4], full[8];
        formatTime(hh, sizeof(hh), mm, sizeof(mm), full, sizeof(full), h, m);
        int16_t x1, y1; uint16_t w, ht;
        c.getTextBounds(full, 0, 0, &x1, &y1, &w, &ht);
        c.setCursor(1 - x1, -y1);
        c.print(full);
        return (int)w;
    };
    int wOld = rasterize(*cOld, (uint8_t)tr.prevHour, (uint8_t)tr.prevMinute);
    int wNew = rasterize(*cNew, hour, minute);

    // Calculamos el desplazamiento vertical: usamos el bbox del NEW (mas
    // relevante visualmente).
    char hh[4], mm[4], full[8];
    formatTime(hh, sizeof(hh), mm, sizeof(mm), full, sizeof(full), hour, minute);
    dma->setFont(font);
    dma->setTextSize(1);
    int16_t x1, y1; uint16_t w, h;
    dma->getTextBounds(full, 0, 0, &x1, &y1, &w, &h);
    int dstTopY = dstBaselineY + (int)y1;

    int dstNewX = dstCenterX - (1 + wNew / 2);
    int dstOldX = dstCenterX - (1 + wOld / 2);
    int canvasW = cNew->width();
    int canvasH = cNew->height();
    float p = (float)elapsed / (float)FADE_MS;
    float aOld = 1.0f - p;
    float aNew = p;
    int minX = (dstNewX < dstOldX) ? dstNewX : dstOldX;
    int maxX = ((dstNewX + canvasW) > (dstOldX + canvasW))
                   ? (dstNewX + canvasW) : (dstOldX + canvasW);
    for (int yy = 0; yy < canvasH; yy++) {
        int yPanel = dstTopY + yy;
        if (yPanel < 0 || yPanel >= HEIGHT) continue;
        for (int xPanel = minX; xPanel < maxX; xPanel++) {
            if (xPanel < 0 || xPanel >= WIDTH) continue;
            int xOldC = xPanel - dstOldX;
            int xNewC = xPanel - dstNewX;
            bool inOld = (xOldC >= 0 && xOldC < canvasW) ? cOld->getPixel(xOldC, yy) : false;
            bool inNew = (xNewC >= 0 && xNewC < canvasW) ? cNew->getPixel(xNewC, yy) : false;
            float a = (inOld ? aOld : 0.0f) + (inNew ? aNew : 0.0f);
            if (a < 0.01f) continue;
            if (a > 1.0f) a = 1.0f;
            dma->drawPixel(xPanel, yPanel, scale565(color, a));
        }
    }
}

// Dibuja un Icons::Frame escalado por `scale` (cada pixel del icono -> bloque
// scale x scale). Usado en modo focus para que el icono 5x5 se vea como 10x10.
static void drawFrameScaled(int x, int y, const Icons::Frame& f, int scale) {
    for (int yy = 0; yy < Icons::ICON_H; yy++) {
        for (int xx = 0; xx < Icons::ICON_W; xx++) {
            uint8_t idx = f.px[yy][xx];
            if (idx == 0) continue;
            uint16_t c = Icons::paletteAs565(idx);
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    dma->drawPixel(x + xx * scale + dx, y + yy * scale + dy, c);
                }
            }
        }
    }
}

void renderFocus(const Row& r, float secondOfMinuteF) {
    if (!dma) return;
    dma->clearScreen();
    dma->setFont(&TomThumb);

    // ---- 1) HH:MM grande con FreeSans12pt7b (~18px alto), CENTRADA en la
    // parte superior con 1px de margen al borde de arriba. Color
    // configurable desde la UI (focus_hour_color), independiente del color
    // de la ciudad.
    int hourBottomY = 0;     // ultimo y del texto de la hora, para centrar la fecha debajo
    if (r.hasTime) {
        uint16_t base = rgb888to565(Config::cfg.focusHourColor);
        float colonAlpha = computeColonAlpha(secondOfMinuteF);
        // baseline en panel: top a 1 px del borde superior. Para FreeSans12pt7b
        // calculamos baselineY usando getTextBounds (independiente del valor
        // exacto de "HH:MM" siempre que use la misma fuente).
        dma->setFont(&FreeSans12pt7b);
        dma->setTextSize(1);
        int16_t x1, y1; uint16_t w, h;
        dma->getTextBounds("88:88", 0, 0, &x1, &y1, &w, &h);
        int baselineY = 1 - (int)y1;
        renderHHMMWithFade(s_focusHHMMTrans,
                           g_focusCanvasOld, g_focusCanvasNew,
                           &FreeSans12pt7b,
                           r.hour, r.minute,
                           Config::cfg.hourLeadingZero,
                           WIDTH / 2, baselineY,
                           base, colonAlpha);
        hourBottomY = 1 + (int)h - 1;
        dma->setFont(&TomThumb);
    }

    // ---- 2) Fila inferior (y entre el bottom de la hora y el segundero):
    // fecha izq | icono animado centro | temp der. El icono usa
    // tickRowAnim(0, ...) para reusar el state de la animacion (mismo slot
    // que la fila 0 del modo normal — no se mezclan porque cada render solo
    // dibuja uno de los dos modos).
    const int SECONDS_BAR_TOP = 30;
    dma->setTextSize(1);
    // Fecha centrada: horizontalmente entre borde izq (x=0) y borde del
    // icono; verticalmente entre el bottom de la hora y el top del segundero.
    if (r.name && *r.name) {
        const int sc = 2;
        int iconX = (WIDTH - Icons::ICON_W * sc) / 2;  // = 27 con scale 2
        int xCenter = iconX / 2;                        // medio del rango [0, iconX)
        int yCenter = (hourBottomY + SECONDS_BAR_TOP) / 2;
        int16_t x1, y1; uint16_t w, h;
        dma->getTextBounds(r.name, 0, 0, &x1, &y1, &w, &h);
        int cursorX = xCenter - (int)w / 2 - (int)x1;
        int cursorY = yCenter - (int)y1 - ((int)h - 1) / 2;
        dma->setTextColor(rgb888to565(Config::cfg.focusDateColor));
        dma->setCursor(cursorX, cursorY);
        dma->print(r.name);
    }
    if (r.hasWeather && r.icon != IconType::NONE) {
        // Si hay preview de icono activo (editando desde la UI) lo usamos;
        // sino, tick del icono normal por meteo.
        const Icons::Frame* frame = nullptr;
        if (g_previewActive) frame = tickPreviewAnim();
        else                 frame = tickRowAnim(0, r.icon);
        if (frame) {
            const int sc = 2;
            int iconX = (WIDTH - Icons::ICON_W * sc) / 2;
            drawFrameScaled(iconX, 19, *frame, sc);
        }
    }
    if (r.hasWeather) {
        // Temperatura TomThumb x2 right-aligned, con º como cuadrado hueco
        // 4x4 escalado para que combine visualmente con la fuente grande.
        char tnum[6];
        snprintf(tnum, sizeof(tnum), "%d", r.tempC);
        dma->setTextSize(2);
        uint16_t tcol = tempColor(r.tempC);
        int16_t x1b, y1b; uint16_t wb, hb;
        dma->getTextBounds(tnum, 0, 0, &x1b, &y1b, &wb, &hb);
        const int DEG_W = 4, DEG_GAP = 2;
        int totalW = (int)wb + DEG_GAP + DEG_W;
        int xStart = WIDTH - totalW - 2;            // 2px margen derecho
        int yBase = 29;                              // baseline en y=29
        dma->setTextColor(tcol);
        dma->setCursor(xStart, yBase);
        dma->print(tnum);
        int degX = xStart + (int)wb + DEG_GAP;
        int degTopY = yBase - 9;
        for (int i = 0; i < 4; i++) {
            dma->drawPixel(degX + i, degTopY,     tcol);
            dma->drawPixel(degX + i, degTopY + 3, tcol);
            dma->drawPixel(degX,     degTopY + i, tcol);
            dma->drawPixel(degX + 3, degTopY + i, tcol);
        }
        dma->setTextSize(1);
    }

    // ---- 4) Segundera smooth (sub-pixel) en y=30..31. ----
    drawSecondsBarSmooth(0, 30, WIDTH, 2, secondOfMinuteF);

    // Overlays compartidos con el modo normal.
    drawActiveRipples();
    drawBrightnessOverlay();
    dma->flipDMABuffer();
}

// ── Modo Claude stats ───────────────────────────────────────────────────
// Layout 64x32 mostrando ambas ventanas (5h y 7d) con pace bar + countdown,
// mas una cabecera compacta con la hora/icono/temp de la primera ciudad.
//
//   y=0..5   : HH:MM | icon 5x5 | TT°  (header con info meteorologica)
//   y=7..11  : "5h NN%  HhMMm"          (stats 5h en texto)
//   y=13..16 : pace bar 5h (4 px alto, color por pace + marker elapsed)
//   y=18..22 : "7d NN%  DdHh"           (idem 7d)
//   y=24..27 : pace bar 7d (4 px alto)
//   y=29..31 : zona libre / pace label de 5h
static void drawClaudePaceBar(int x0, int y0, int barW, int height,
                              double used, double elapsed, uint32_t color888) {
    uint16_t bg     = rgb888to565(0x202020);
    uint16_t fill   = rgb888to565(color888);
    uint16_t marker = rgb888to565(0xFFFFFF);
    if (used < 0) used = 0; if (used > 1) used = 1;
    if (elapsed < 0) elapsed = 0; if (elapsed > 1) elapsed = 1;
    int fillW = (int)(used * barW + 0.5);
    int mx    = (int)(elapsed * barW + 0.5);
    if (mx >= barW) mx = barW - 1;
    for (int x = 0; x < barW; x++) {
        uint16_t c = (x < fillW) ? fill : bg;
        for (int dy = 0; dy < height; dy++) dma->drawPixel(x0 + x, y0 + dy, c);
    }
    if (mx >= 0) {
        for (int dy = 0; dy < height; dy++) dma->drawPixel(x0 + mx, y0 + dy, marker);
    }
}

// Layout 2/3 + 1/3: columna izquierda 42 px con stats de Claude (5h + 7d
// + pace label), columna derecha 21 px con hora, fecha, temp e icono de la
// primera ciudad apilados de arriba a abajo y centrados.
void renderClaude(const Row& weatherRow, const ClaudeView& cv, float secondOfMinuteF) {
    if (!dma) return;
    dma->clearScreen();
    dma->setFont(&TomThumb);
    dma->setTextSize(1);

    // Colores hora/fecha: reutilizamos los pickers de la web focus_hour_color
    // y focus_date_color tambien para el modo Claude. dim y dim2 son fijos
    // para los stats (no expuestos en la UI).
    uint16_t hourCol = rgb888to565(Config::cfg.focusHourColor);
    uint16_t dateCol = rgb888to565(Config::cfg.focusDateColor);
    uint16_t dim     = rgb888to565(0xAAAAAA);
    uint16_t dim2    = rgb888to565(0x666666);

    const int LEFT_W   = (WIDTH * 2) / 3;        // 42
    const int RIGHT_X  = LEFT_W + 1;             // 43, deja 1 px de gap
    const int RIGHT_W  = WIDTH - RIGHT_X;        // 21
    const int RIGHT_CX = RIGHT_X + RIGHT_W / 2;  // x medio para centrar

    // Separador vertical fino entre las dos columnas. Termina 2 px antes
    // del bottom para que la barra de segundos de abajo recorra la pantalla
    // de punta a punta sin interrumpirse.
    uint16_t sepCol = rgb888to565(0x202020);
    for (int y = 0; y < HEIGHT - 2; y++) dma->drawPixel(LEFT_W, y, sepCol);

    // ── LEFT 2/3: Claude stats ─────────────────────────────────────────
    //
    // Helper: dibuja countdown + simbolo de "tiempo" (4x5 px) a su derecha,
    // todo alineado a la derecha y en gris oscuro discreto.
    auto drawCountdownRight = [&](const char* cd, int yBaseline) {
        // Simbolo (4 ancho x 5 alto):
        //   . . . .
        //   . X X X
        //   . . . X
        //   . . X X
        //   . . . X
        static const uint8_t SYM[5] = {
            0b0000,
            0b0111,
            0b0001,
            0b0011,
            0b0001,
        };
        const int SYM_W = 4, SYM_GAP = 0;   // el sprite ya tiene su propio margen izq
        int16_t x1, y1; uint16_t w, h;
        dma->getTextBounds(cd, 0, 0, &x1, &y1, &w, &h);
        int blockW = (int)w + SYM_GAP + SYM_W;
        // Ultimo pixel del simbolo en LEFT_W-2 (col 40 con LEFT_W=42 → 1 px
        // de gap antes del separador en col 42).
        int textX = (LEFT_W - 1) - blockW - (int)x1;
        if (textX < 0) textX = 0;
        uint16_t col = rgb888to565(0x404040);
        dma->setTextColor(col);
        dma->setCursor(textX, yBaseline);
        dma->print(cd);
        // Simbolo: top-left en (sx0, sy0). Lo alineamos con el top del texto
        // (top de TomThumb = baseline - 4).
        int sx0 = textX + (int)x1 + (int)w + SYM_GAP;
        int sy0 = yBaseline - 5;   // 1 px arriba
        for (int yy = 0; yy < 5; yy++) {
            uint8_t row = SYM[yy];
            for (int xx = 0; xx < 4; xx++) {
                if (row & (0x8 >> xx)) dma->drawPixel(sx0 + xx, sy0 + yy, col);
            }
        }
    };

    // Linea 5h: "5h NN%" izquierda + countdown right (XhYYm, gris oscuro).
    if (cv.fiveValid) {
        char line[12];
        snprintf(line, sizeof(line), "5h %d%%", (int)(cv.fiveUsed * 100.0 + 0.5));
        dma->setTextColor(dim);
        dma->setCursor(0, 6);
        dma->print(line);
        char cd[10];
        snprintf(cd, sizeof(cd), "%ldm", cv.fiveRemainingSec / 60);
        drawCountdownRight(cd, 6);
    } else {
        dma->setTextColor(dim2);
        dma->setCursor(0, 6);
        dma->print(cv.hasData ? "5h -" : "5h ...");
    }
    // Pace bar 5h: ancho LEFT_W-1 → deja 1 px de gap a la izquierda del
    // separador. Bar termina en x=40 con LEFT_W=42 (separador en x=42).
    if (cv.fiveValid) {
        drawClaudePaceBar(0, 7, LEFT_W - 1, 4,
                          cv.fiveUsed, cv.fiveElapsed, cv.fiveColor);
    }

    // Linea 7d: "7d NN%" izquierda + countdown right (XdYYh si >=1d, sino XhYYm).
    if (cv.sevenValid) {
        char line[12];
        snprintf(line, sizeof(line), "7d %d%%", (int)(cv.sevenUsed * 100.0 + 0.5));
        dma->setTextColor(dim);
        dma->setCursor(0, 17);
        dma->print(line);
        char cd[10];
        snprintf(cd, sizeof(cd), "%ldh", cv.sevenRemainingSec / 3600);
        drawCountdownRight(cd, 17);
    } else {
        dma->setTextColor(dim2);
        dma->setCursor(0, 17);
        dma->print(cv.hasData ? "7d -" : "7d ...");
    }
    if (cv.sevenValid) {
        drawClaudePaceBar(0, 18, LEFT_W - 1, 4,
                          cv.sevenUsed, cv.sevenElapsed, cv.sevenColor);
    }

    // Pace label 5h centrada en la columna izquierda (y=29 baseline, top
    // alrededor de y=25).
    if (cv.fiveValid && cv.fiveLabel && *cv.fiveLabel) {
        dma->setTextColor(rgb888to565(cv.fiveColor));
        int16_t x1, y1; uint16_t w, h;
        dma->getTextBounds(cv.fiveLabel, 0, 0, &x1, &y1, &w, &h);
        int x = (LEFT_W - (int)w) / 2 - (int)x1;
        if (x < 0) x = 0;
        dma->setCursor(x, 28);
        dma->print(cv.fiveLabel);
    }

    // ── RIGHT 1/3: hora | fecha | temp | icono (centrados, top→bottom) ──
    // 4 elementos en 32 px → baselines a y=6 / 14 / 22, icono y=24..28.
    auto centerInRight = [&](int textW) -> int {
        int x = RIGHT_CX - textW / 2;
        if (x < RIGHT_X) x = RIGHT_X;
        return x;
    };

    // Hora HH:MM con crossfade + colon blink. Reutiliza el helper compartido.
    if (weatherRow.hasTime) {
        float colonAlpha = computeColonAlpha(secondOfMinuteF);
        renderHHMMWithFade(s_claudeHHMMTrans,
                           g_claudeCanvasOld, g_claudeCanvasNew,
                           &TomThumb,
                           weatherRow.hour, weatherRow.minute,
                           Config::cfg.hourLeadingZero,
                           RIGHT_CX, 6,
                           hourCol, colonAlpha);
    }

    // Fecha siempre en formato DD/MM en este modo (ignora dateFormatText),
    // porque "8 May" + el resto de elementos satura visualmente la columna
    // de 21 px. El modo focus sigue respetando la opcion del usuario.
    if (weatherRow.hasTime && weatherRow.day > 0 && weatherRow.month > 0) {
        char dateBuf[10];
        snprintf(dateBuf, sizeof(dateBuf), "%02u/%02u",
                 weatherRow.day, weatherRow.month);
        dma->setTextColor(dateCol);
        int16_t x1, y1; uint16_t w, h;
        dma->getTextBounds(dateBuf, 0, 0, &x1, &y1, &w, &h);
        dma->setCursor(centerInRight((int)w) - (int)x1, 12);
        dma->print(dateBuf);
    }

    // Fila 3: icono del tiempo (animado) + temperatura uno al lado del otro,
    // bloque centrado en la columna derecha. y=14..18.
    if (weatherRow.hasWeather) {
        char tnum[6];
        snprintf(tnum, sizeof(tnum), "%d", weatherRow.tempC);
        uint16_t tc = tempColor(weatherRow.tempC);
        int16_t x1, y1; uint16_t w, h;
        dma->getTextBounds(tnum, 0, 0, &x1, &y1, &w, &h);
        const int ICON_GAP = 2;
        const int DEG_W = 2, DEG_GAP = 1;
        int tempW = (int)w + DEG_GAP + DEG_W;
        bool hasIcon = (weatherRow.icon != IconType::NONE);
        int totalW = (hasIcon ? Icons::ICON_W + ICON_GAP : 0) + tempW;
        int xStart = RIGHT_CX - totalW / 2;
        if (xStart < RIGHT_X) xStart = RIGHT_X;
        int iconY = 13;   // icono 2 px arriba (antes 15)
        int yBase = 18;   // baseline temp 2 px arriba (antes 20)
        if (hasIcon) {
            const Icons::Frame* frame = nullptr;
            if (g_previewActive) frame = tickPreviewAnim();
            else                 frame = tickRowAnim(0, weatherRow.icon);
            if (frame) drawFrame(xStart, iconY, *frame);
        }
        int tempX = xStart + (hasIcon ? Icons::ICON_W + ICON_GAP : 0);
        dma->setTextColor(tc);
        dma->setCursor(tempX - (int)x1, yBase);
        dma->print(tnum);
        int degX = tempX + (int)w + DEG_GAP;
        int degTopY = yBase - 5;
        for (int yy = 0; yy < 2; yy++)
            for (int xx = 0; xx < 2; xx++)
                dma->drawPixel(degX + xx, degTopY + yy, tc);
    }

    // Tick "happy": de vez en cuando Clawd pone cara feliz (parpado inferior
    // en V invertida). True durante el periodo de animacion. Cada 8-20s pone
    // cara feliz durante 4-10s. Mientras dura, no parpadea ni mira a los
    // lados (decidido en el sitio donde se usan estos valores).
    auto clawdIsHappy = []() -> bool {
        static uint32_t s_nextAtMs = 0;
        static uint32_t s_startMs  = 0;
        static uint32_t s_durMs    = 0;
        uint32_t now = millis();
        if (s_startMs == 0) {
            if (s_nextAtMs == 0) {
                s_nextAtMs = now + 8000 + (esp_random() % 12000);
            }
            if (now < s_nextAtMs) return false;
            s_startMs = now;
            s_durMs   = 4000 + (esp_random() % 6000);
        }
        if ((now - s_startMs) >= s_durMs) {
            s_startMs = 0;
            s_nextAtMs = now + 8000 + (esp_random() % 12000);
            return false;
        }
        return true;
    };

    // Tick del "look": de vez en cuando Clawd mira a un lado. Devuelve
    // desplazamiento horizontal de los ojos: -1 (izq), 0 (centro), +1 (der).
    // Patron: cada 4-10s mira a un lado durante 0.8-2.3s.
    auto clawdEyeOffset = []() -> int {
        static uint32_t s_nextLookAtMs = 0;
        static uint32_t s_lookStartMs  = 0;
        static int8_t   s_lookOffset   = 0;
        static uint32_t s_lookDurMs    = 0;
        uint32_t now = millis();
        if (s_lookStartMs == 0) {
            if (s_nextLookAtMs == 0) {
                s_nextLookAtMs = now + 4000 + (esp_random() % 6000);
            }
            if (now < s_nextLookAtMs) return 0;
            s_lookOffset = (esp_random() & 1) ? -1 : 1;
            s_lookStartMs = now;
            s_lookDurMs   = 800 + (esp_random() % 1500);
        }
        if ((now - s_lookStartMs) >= s_lookDurMs) {
            s_lookStartMs   = 0;
            s_nextLookAtMs  = now + 4000 + (esp_random() % 6000);
            return 0;
        }
        return (int)s_lookOffset;
    };

    // Tick del guiño de Clawd. Devuelve cuantos rows del ojo estan
    // "cerrados" en este frame (0 = abierto, 1 = medio, 2 = cerrado).
    // Patron: cada 6-15s un ciclo de 2 guiños seguidos.
    auto clawdEyeRowsClosed = []() -> int {
        static uint32_t s_blinkNextAtMs = 0;
        static uint32_t s_blinkStartMs  = 0;
        static int      s_blinkRemaining = 0;
        uint32_t now = millis();
        if (s_blinkStartMs == 0) {
            if (s_blinkNextAtMs == 0) {
                s_blinkNextAtMs = now + 6000 + (esp_random() % 9000);
            }
            if (now < s_blinkNextAtMs) return 0;
            // Empezamos un ciclo de 2 guiños.
            s_blinkRemaining = 2;
            s_blinkStartMs = now;
        }
        uint32_t elapsed = now - s_blinkStartMs;
        // Animacion de un guiño individual: 60ms cerrando, 120ms cerrado,
        // 60ms abriendo, 60ms abierto = 300ms total.
        if (elapsed >= 300) {
            s_blinkRemaining--;
            if (s_blinkRemaining > 0) {
                s_blinkStartMs = now;       // segundo guiño inmediato
                elapsed = 0;
            } else {
                s_blinkStartMs = 0;
                s_blinkNextAtMs = now + 6000 + (esp_random() % 9000);
                return 0;
            }
        }
        if (elapsed < 60)  return 1;        // bajando
        if (elapsed < 180) return 2;        // cerrado
        if (elapsed < 240) return 1;        // subiendo
        return 0;                            // ojo abierto antes del siguiente
    };

    // Fila 4: pixel art de Clawd (mascot naranja de Anthropic). 14x10 con:
    //   - cabeza recta arriba y abajo (sin redondeo)
    //   - 2 ojos rectangulares verticales
    //   - bracitos sobresaliendo 2 px a cada lado del cuerpo (rows 4-5)
    //   - 4 patitas en la base
    // Cuerpo 10 wide centrado; bracitos suman 2 px a cada lado = 14 total.
    {
        // Sprite SIN ojos. Los huecos se pintan despues, asi podemos
        // desplazarlos para "mirar" a un lado sin tocar el sprite base.
        static const uint16_t CLAWD[10] = {
            0b00111111111100,   // ..XXXXXXXXXX..   cabeza top recta
            0b00111111111100,   // ..XXXXXXXXXX..
            0b00111111111100,   // ..XXXXXXXXXX..   (ojos pintados despues)
            0b00111111111100,   // ..XXXXXXXXXX..
            0b11111111111111,   // XXXXXXXXXXXXXX   bracitos full
            0b11111111111111,   // XXXXXXXXXXXXXX
            0b00111111111100,   // ..XXXXXXXXXX..
            0b00111111111100,   // ..XXXXXXXXXX..   bottom recto
            0b00110100101100,   // ..XX.X..X.XX..   patas
            0b00110100101100,   // ..XX.X..X.XX..
        };
        uint16_t orange = rgb888to565(0xE07A2F);
        int x0 = RIGHT_CX - 7;
        int y0 = 19;
        for (int yy = 0; yy < 10; yy++) {
            uint16_t row = CLAWD[yy];
            for (int xx = 0; xx < 14; xx++) {
                if (row & (0x2000 >> xx)) dma->drawPixel(x0 + xx, y0 + yy, orange);
            }
        }
        // Pintar ojos: posicion base cols 4 y 9, desplazada por eyeOff.
        // Si Clawd esta "feliz": mira al frente y no parpadea (forzamos
        // eyeOff=0 y eyeRows=0 sin llamar los otros ticks para que sus
        // timers no se queden mid-animacion). El patron feliz pinta 4
        // huecos extra en la row inferior tipo "^_^".
        bool happy = clawdIsHappy();
        int eyeOff  = happy ? 0 : clawdEyeOffset();
        int eyeRows = happy ? 0 : clawdEyeRowsClosed();
        int eyeLX = x0 + 4 + eyeOff;
        int eyeRX = x0 + 9 + eyeOff;
        // Pintar negro (huecos) los 2 rows de cada ojo.
        dma->drawPixel(eyeLX, y0 + 2, 0);
        dma->drawPixel(eyeLX, y0 + 3, 0);
        dma->drawPixel(eyeRX, y0 + 2, 0);
        dma->drawPixel(eyeRX, y0 + 3, 0);
        // Mano derecha saludando cuando esta feliz. Los 2 px de la punta
        // (col 13 rows 4-5 del sprite) se levantan 1 row a rows 3-4, y
        // vuelven a su posicion normal, alternando cada ~350 ms.
        if (happy) {
            bool handUp = ((millis() / 350) & 1) == 0;
            if (handUp) {
                dma->drawPixel(x0 + 13, y0 + 5, 0);        // limpia la punta inferior
                dma->drawPixel(x0 + 13, y0 + 3, orange);   // pinta la mano un row mas arriba
            }
        }

        // Patron feliz (V invertida): la row 2 mantiene los 2 huecos del ojo;
        // la row 3 rellena el centro de los ojos (col base) y abre huecos en
        // las dos cols adyacentes, formando un "^".
        if (happy) {
            dma->drawPixel(eyeLX,     y0 + 3, orange);
            dma->drawPixel(eyeRX,     y0 + 3, orange);
            dma->drawPixel(eyeLX - 1, y0 + 3, 0);
            dma->drawPixel(eyeLX + 1, y0 + 3, 0);
            dma->drawPixel(eyeRX - 1, y0 + 3, 0);
            dma->drawPixel(eyeRX + 1, y0 + 3, 0);
        }
        // Guiño: reponer naranja desde arriba segun frame. Si esta feliz y se
        // cierra del todo, tapamos tambien los huecos adyacentes para que la
        // cara feliz desaparezca cuando los ojos cierran.
        if (eyeRows >= 1) {
            dma->drawPixel(eyeLX, y0 + 2, orange);
            dma->drawPixel(eyeRX, y0 + 2, orange);
        }
        if (eyeRows >= 2) {
            dma->drawPixel(eyeLX, y0 + 3, orange);
            dma->drawPixel(eyeRX, y0 + 3, orange);
            if (happy) {
                dma->drawPixel(eyeLX - 1, y0 + 3, orange);
                dma->drawPixel(eyeLX + 1, y0 + 3, orange);
                dma->drawPixel(eyeRX - 1, y0 + 3, orange);
                dma->drawPixel(eyeRX + 1, y0 + 3, orange);
            }
        }
    }

    // ── Barra de segundos full width en y=30..31, sub-pixel smooth.
    drawSecondsBarSmooth(0, 30, WIDTH, 2, secondOfMinuteF);

    drawActiveRipples();
    drawBrightnessOverlay();
    dma->flipDMABuffer();
}

// ── Modo Game of Life ───────────────────────────────────────────────────
// Conway en una grid 64x24 (parte superior) + fila inferior con hora,
// fecha, icono y temperatura. Toroidal (los bordes envuelven). Si el
// patron queda estatico o entra en un ciclo de periodo <=4 durante 10s,
// reseed con un seed aleatorio nuevo.
static constexpr int LIFE_W = 64;
// 23 rows (y=0..22) deja un 1 px de gap negro en y=23, entre el grid y la
// fila inferior con hora/fecha/temp (que empieza en y=24 segun glyph).
static constexpr int LIFE_H = 23;
static uint64_t s_lifeGrid[LIFE_H] = {0};
static uint64_t s_lifeNext[LIFE_H] = {0};
static uint32_t s_lifeHistHash[4] = {0, 0, 0, 0};
static uint32_t s_lifeStuckSinceMs = 0;
static uint32_t s_lifeLastStepMs = 0;
static uint16_t s_lifeHue = 0;     // 0..359, avanza cada step en modo rainbow
static bool     s_lifeInited = false;
static constexpr uint32_t LIFE_STEP_MS = 150;
static constexpr uint32_t LIFE_STUCK_MS = 10000;
static constexpr int      LIFE_HUE_STEP = 3;   // grados de variacion por step

// HSV (S=V=full) -> RGB565. h en grados 0..359.
static uint16_t hueToRgb565(int h) {
    if (h < 0) h = (h % 360 + 360) % 360;
    if (h >= 360) h %= 360;
    int region = h / 60;
    int rem = (h - region * 60) * 255 / 60;
    int r, g, b;
    switch (region) {
        case 0: r = 255; g = rem; b = 0; break;
        case 1: r = 255 - rem; g = 255; b = 0; break;
        case 2: r = 0; g = 255; b = rem; break;
        case 3: r = 0; g = 255 - rem; b = 255; break;
        case 4: r = rem; g = 0; b = 255; break;
        default: r = 255; g = 0; b = 255 - rem; break;
    }
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint32_t lifeHash() {
    // FNV-1a 32 bits sobre las 24 rows de 64 bits.
    uint32_t h = 2166136261u;
    for (int y = 0; y < LIFE_H; y++) {
        uint64_t r = s_lifeGrid[y];
        h ^= (uint32_t)(r & 0xFFFFFFFFu); h *= 16777619u;
        h ^= (uint32_t)(r >> 32);         h *= 16777619u;
    }
    return h;
}

static void lifeReseed() {
    for (int y = 0; y < LIFE_H; y++) {
        uint64_t row = 0;
        for (int x = 0; x < LIFE_W; x++) {
            if ((esp_random() & 0xFF) < 80) row |= (1ULL << x);   // ~31% alive
        }
        s_lifeGrid[y] = row;
    }
    s_lifeHistHash[0] = lifeHash();
    s_lifeHistHash[1] = 0;
    s_lifeHistHash[2] = 0;
    s_lifeHistHash[3] = 0;
    s_lifeStuckSinceMs = 0;
}

static inline bool lifeAlive(int x, int y) {
    if (x < 0) x += LIFE_W;
    if (x >= LIFE_W) x -= LIFE_W;
    if (y < 0) y += LIFE_H;
    if (y >= LIFE_H) y -= LIFE_H;
    return (s_lifeGrid[y] >> x) & 1ULL;
}

static void lifeStep() {
    for (int y = 0; y < LIFE_H; y++) {
        uint64_t row = 0;
        for (int x = 0; x < LIFE_W; x++) {
            int n = (int)lifeAlive(x-1, y-1) + (int)lifeAlive(x, y-1) + (int)lifeAlive(x+1, y-1)
                  + (int)lifeAlive(x-1, y)                            + (int)lifeAlive(x+1, y)
                  + (int)lifeAlive(x-1, y+1) + (int)lifeAlive(x, y+1) + (int)lifeAlive(x+1, y+1);
            bool alive = (s_lifeGrid[y] >> x) & 1ULL;
            if ((alive && (n == 2 || n == 3)) || (!alive && n == 3)) row |= (1ULL << x);
        }
        s_lifeNext[y] = row;
    }
    for (int y = 0; y < LIFE_H; y++) s_lifeGrid[y] = s_lifeNext[y];
}

void renderLife(const Row& wRow, float secondOfMinuteF) {
    if (!dma) return;
    if (!s_lifeInited) {
        lifeReseed();
        s_lifeInited = true;
        s_lifeLastStepMs = millis();
    }

    uint32_t now = millis();
    if (now - s_lifeLastStepMs >= Config::cfg.lifeStepMs) {
        s_lifeLastStepMs = now;
        lifeStep();
        s_lifeHue = (s_lifeHue + LIFE_HUE_STEP) % 360;
        uint32_t newHash = lifeHash();
        // Detectamos ciclo si el nuevo hash coincide con alguno de los 4
        // anteriores (cubre periodos 1..4 — incluye estatico, blinkers,
        // toads y la mayoria de oscilladores comunes).
        bool stuck = false;
        for (int i = 0; i < 4; i++) {
            if (s_lifeHistHash[i] != 0 && s_lifeHistHash[i] == newHash) {
                stuck = true; break;
            }
        }
        if (stuck) {
            if (s_lifeStuckSinceMs == 0) s_lifeStuckSinceMs = now;
            if (now - s_lifeStuckSinceMs >= LIFE_STUCK_MS) {
                lifeReseed();
                return;
            }
        } else {
            s_lifeStuckSinceMs = 0;
        }
        // Shift de la historia.
        s_lifeHistHash[3] = s_lifeHistHash[2];
        s_lifeHistHash[2] = s_lifeHistHash[1];
        s_lifeHistHash[1] = s_lifeHistHash[0];
        s_lifeHistHash[0] = newHash;
    }

    dma->clearScreen();

    // Pintar celulas vivas. Color configurable desde la web; en modo
    // "rainbow" el hue avanza 3 grados en cada step.
    uint16_t cellColor = Config::cfg.lifeRainbow
                           ? hueToRgb565(s_lifeHue)
                           : rgb888to565(Config::cfg.lifeColor);
    for (int y = 0; y < LIFE_H; y++) {
        uint64_t row = s_lifeGrid[y];
        if (!row) continue;
        for (int x = 0; x < LIFE_W; x++) {
            if ((row >> x) & 1ULL) dma->drawPixel(x, y, cellColor);
        }
    }

    // Fila inferior compartida con renderImage.
    drawCommonBottomRow(wRow, secondOfMinuteF);

    drawActiveRipples();
    drawBrightnessOverlay();
    dma->flipDMABuffer();
}

// ── Helper compartido: fila inferior con hora / fecha / icono / temp +
// segundera. Usado por renderLife y renderImage.
static void drawCommonBottomRow(const Row& wRow, float secondOfMinuteF) {
    dma->setFont(&TomThumb);
    dma->setTextSize(1);
    uint16_t hourCol = rgb888to565(Config::cfg.focusHourColor);
    uint16_t dateCol = rgb888to565(Config::cfg.focusDateColor);
    int baseline = 29;
    if (wRow.hasTime) {
        char hh[4], mm[4], full[8];
        bool lz = Config::cfg.hourLeadingZero || wRow.hour >= 10;
        if (lz) snprintf(hh, sizeof(hh), "%02u", wRow.hour);
        else    snprintf(hh, sizeof(hh), "%u",  wRow.hour);
        snprintf(mm, sizeof(mm), "%02u", wRow.minute);
        snprintf(full, sizeof(full), "%s:%s", hh, mm);
        float colonAlpha = computeColonAlpha(secondOfMinuteF);
        int16_t x1, y1; uint16_t w, h;
        dma->getTextBounds(full, 0, 0, &x1, &y1, &w, &h);
        dma->setCursor(0, baseline);
        dma->setTextColor(hourCol);
        dma->print(hh);
        dma->setTextColor(scale565(hourCol, colonAlpha));
        dma->print(":");
        dma->setTextColor(hourCol);
        dma->print(mm);
    }
    if (wRow.hasTime && wRow.day > 0 && wRow.month > 0) {
        char d[8];
        snprintf(d, sizeof(d), "%02u/%02u", wRow.day, wRow.month);
        int16_t x1, y1; uint16_t w, h;
        dma->getTextBounds(d, 0, 0, &x1, &y1, &w, &h);
        int x = (WIDTH - (int)w) / 2 - (int)x1;
        dma->setTextColor(dateCol);
        dma->setCursor(x, baseline);
        dma->print(d);
    }
    if (wRow.hasWeather) {
        char tnum[6];
        snprintf(tnum, sizeof(tnum), "%d", wRow.tempC);
        uint16_t tc = tempColor(wRow.tempC);
        int16_t x1, y1; uint16_t w, h;
        dma->getTextBounds(tnum, 0, 0, &x1, &y1, &w, &h);
        const int DEG_W = 2, DEG_GAP = 1;
        int totalW = (int)w + DEG_GAP + DEG_W;
        int xStart = WIDTH - totalW;
        dma->setTextColor(tc);
        dma->setCursor(xStart, baseline);
        dma->print(tnum);
        int degX = xStart + (int)w + DEG_GAP;
        int degTopY = baseline - 5;
        for (int yy = 0; yy < 2; yy++)
            for (int xx = 0; xx < 2; xx++)
                dma->drawPixel(degX + xx, degTopY + yy, tc);
        if (wRow.icon != IconType::NONE) {
            const Icons::Frame* frame = nullptr;
            if (g_previewActive) frame = tickPreviewAnim();
            else                 frame = tickRowAnim(0, wRow.icon);
            if (frame) drawFrame(xStart - Icons::ICON_W - 2, 24, *frame);
        }
    }
    drawSecondsBarSmooth(0, 30, WIDTH, 2, secondOfMinuteF);
}

// ── Modo IMAGE ─────────────────────────────────────────────────────────
// La imagen es 64x23 RGB565 little-endian, persistida en /userimg.bin
// (2944 bytes). Se carga al boot y se recarga tras cada upload. Si no
// hay imagen valida, muestra un placeholder.
static constexpr int USERIMG_W = 64;
static constexpr int USERIMG_H = 23;
static uint16_t s_userImg[USERIMG_W * USERIMG_H] = {0};
static bool     s_userImgLoaded = false;

void reloadUserImage() {
    if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
        s_userImgLoaded = false; return;
    }
    File f = LittleFS.open("/userimg.bin", "r");
    if (!f) { s_userImgLoaded = false; return; }
    size_t want = sizeof(s_userImg);
    size_t got  = f.read((uint8_t*)s_userImg, want);
    f.close();
    s_userImgLoaded = (got == want);
    Serial.printf("[userimg] reload: got=%u want=%u ok=%d\n",
                  (unsigned)got, (unsigned)want, (int)s_userImgLoaded);
}

void renderImage(const Row& wRow, float secondOfMinuteF) {
    if (!dma) return;
    dma->clearScreen();
    if (s_userImgLoaded) {
        for (int y = 0; y < USERIMG_H; y++) {
            for (int x = 0; x < USERIMG_W; x++) {
                uint16_t c = s_userImg[y * USERIMG_W + x];
                if (c) dma->drawPixel(x, y, c);
            }
        }
    } else {
        dma->setFont(&TomThumb);
        dma->setTextSize(1);
        dma->setTextColor(rgb888to565(0x808080));
        const char* msg = "upload an image";
        int16_t x1, y1; uint16_t w, h;
        dma->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
        dma->setCursor((WIDTH - (int)w) / 2 - (int)x1, 14);
        dma->print(msg);
    }
    drawCommonBottomRow(wRow, secondOfMinuteF);
    drawActiveRipples();
    drawBrightnessOverlay();
    dma->flipDMABuffer();
}

// ── Modo Llama (Doom fire) ─────────────────────────────────────────────
// Buffer 64x23 con indices a paleta 0..36. Row 22 = "fuel line" con max
// intensidad; rows 0..21 se calculan propagando hacia arriba con shift
// horizontal y decay aleatorios, dando flickering de llamas.
static constexpr int FIRE_W = 64;
static constexpr int FIRE_H = 23;
static uint8_t  s_fireBuf[FIRE_W * FIRE_H];
static bool     s_fireInited = false;
static constexpr int FIRE_PAL = 37;
static uint16_t s_firePal565[FIRE_PAL];

// Genera una paleta de fuego para un color base arbitrario. Pasa por:
// idx 0 (negro) → idx ~mid (color base, brillo medio) → tono cada vez mas
// claro → blanco. Mantiene la sensacion de "llama" para colores no clasicos.
static void buildFirePaletteFromColor(uint32_t baseRgb) {
    int br = (baseRgb >> 16) & 0xFF;
    int bg = (baseRgb >> 8)  & 0xFF;
    int bb =  baseRgb        & 0xFF;
    for (int i = 0; i < FIRE_PAL; i++) {
        float t = (float)i / (float)(FIRE_PAL - 1);
        int r, g, b;
        if (t < 0.5f) {
            // 0..50%: negro → color base (intensidad x2 para llegar al base
            // saturado en mid).
            float p = t * 2.0f;
            r = (int)(br * p);
            g = (int)(bg * p);
            b = (int)(bb * p);
        } else {
            // 50..100%: color base → blanco (desatura subiendo los canales
            // bajos hacia 255).
            float p = (t - 0.5f) * 2.0f;
            r = br + (int)((255 - br) * p);
            g = bg + (int)((255 - bg) * p);
            b = bb + (int)((255 - bb) * p);
        }
        if (r < 0) r = 0; if (r > 255) r = 255;
        if (g < 0) g = 0; if (g > 255) g = 255;
        if (b < 0) b = 0; if (b > 255) b = 255;
        uint32_t rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        s_firePal565[i] = rgb888to565(rgb);
    }
}

static void initFirePalette() {
    // Paleta clasica Lode Vandevenne / Doom: black → red → orange → yellow → white.
    static const uint8_t pal[FIRE_PAL][3] = {
        {0x07,0x07,0x07},{0x1F,0x07,0x07},{0x2F,0x0F,0x07},{0x47,0x0F,0x07},
        {0x57,0x17,0x07},{0x67,0x1F,0x07},{0x77,0x1F,0x07},{0x8F,0x27,0x07},
        {0x9F,0x2F,0x07},{0xAF,0x3F,0x07},{0xBF,0x47,0x07},{0xC7,0x47,0x07},
        {0xDF,0x4F,0x07},{0xDF,0x57,0x07},{0xDF,0x57,0x07},{0xD7,0x5F,0x07},
        {0xD7,0x5F,0x07},{0xD7,0x67,0x0F},{0xCF,0x6F,0x0F},{0xCF,0x77,0x0F},
        {0xCF,0x7F,0x0F},{0xCF,0x87,0x17},{0xC7,0x87,0x17},{0xC7,0x8F,0x17},
        {0xC7,0x97,0x1F},{0xBF,0x9F,0x1F},{0xBF,0x9F,0x1F},{0xBF,0xA7,0x27},
        {0xBF,0xA7,0x27},{0xBF,0xAF,0x2F},{0xB7,0xAF,0x2F},{0xB7,0xB7,0x2F},
        {0xB7,0xB7,0x37},{0xCF,0xCF,0x6F},{0xDF,0xDF,0x9F},{0xEF,0xEF,0xC7},
        {0xFF,0xFF,0xFF},
    };
    for (int i = 0; i < FIRE_PAL; i++) {
        uint32_t rgb = ((uint32_t)pal[i][0] << 16) | ((uint32_t)pal[i][1] << 8) | pal[i][2];
        s_firePal565[i] = rgb888to565(rgb);
    }
}

static void fireInit() {
    memset(s_fireBuf, 0, sizeof(s_fireBuf));
    for (int x = 0; x < FIRE_W; x++) {
        s_fireBuf[(FIRE_H - 1) * FIRE_W + x] = FIRE_PAL - 1;
    }
    initFirePalette();
    s_fireInited = true;
}

static void fireStep() {
    // Bottom row siempre al maximo (sin flicker, asi no se ven columnas
    // negras subiendo). Para que la parte superior quede oscura, usamos un
    // decay mas agresivo (0..3 en lugar de 0..1) — la mayoria de llamas
    // mueren antes de llegar arriba, dejando fondo negro.
    for (int x = 0; x < FIRE_W; x++) {
        s_fireBuf[(FIRE_H - 1) * FIRE_W + x] = FIRE_PAL - 1;
    }
    for (int x = 0; x < FIRE_W; x++) {
        for (int y = 1; y < FIRE_H; y++) {
            int src = y * FIRE_W + x;
            uint8_t pixel = s_fireBuf[src];
            if (pixel == 0) {
                s_fireBuf[src - FIRE_W] = 0;
            } else {
                int rnd  = (int)(esp_random() & 3);   // 0..3 → shift horizontal
                int rnd2 = (int)((esp_random() & 3) + (esp_random() & 1));   // 0..4 → decay (avg 2.0)
                int dstX = x + 1 - rnd;
                if (dstX < 0) dstX = 0;
                if (dstX >= FIRE_W) dstX = FIRE_W - 1;
                int dst = (y - 1) * FIRE_W + dstX;
                s_fireBuf[dst] = (pixel > rnd2) ? (pixel - rnd2) : 0;
            }
        }
    }
}

// Tabla precomputada de sin (256 entradas, -127..127). Inicializada en el
// primer uso. Mucho mas rapida que sinf() para plasma y moire.
static int8_t s_sinTable[256];
static bool   s_sinTableInited = false;
static void initSinTable() {
    if (s_sinTableInited) return;
    for (int i = 0; i < 256; i++) {
        float a = (float)i * 2.0f * 3.14159265f / 256.0f;
        s_sinTable[i] = (int8_t)(sinf(a) * 127.0f);
    }
    s_sinTableInited = true;
}
static inline int sin8(int a) { return s_sinTable[a & 0xFF]; }

// Plasma: superposicion de 4 ondas sinusoidales con offsets temporales.
// Llena s_fireBuf con indices a paleta 0..FIRE_PAL-1.
static void plasmaStep(uint32_t t) {
    int tt = (int)(t / 30);
    for (int y = 0; y < FIRE_H; y++) {
        for (int x = 0; x < FIRE_W; x++) {
            int v = sin8(x * 8 + tt)
                  + sin8(y * 8 + tt * 2)
                  + sin8((x + y) * 4 + tt)
                  + sin8(((x - FIRE_W / 2) * (x - FIRE_W / 2) +
                          (y - FIRE_H / 2) * (y - FIRE_H / 2)) / 4 + tt);
            // v en -508..508. Mapear a 0..FIRE_PAL-1.
            int idx = ((v + 512) * FIRE_PAL) / 1024;
            if (idx < 0) idx = 0;
            if (idx >= FIRE_PAL) idx = FIRE_PAL - 1;
            s_fireBuf[y * FIRE_W + x] = (uint8_t)idx;
        }
    }
}

// Moire: interferencia de dos series de ondas concentricas con centros
// moviendose. Clasico efecto de los 80s.
static void moireStep(uint32_t t) {
    int tt = (int)(t / 30);
    int cx1 = FIRE_W / 2 + (sin8(tt)       * FIRE_W) / 256;
    int cy1 = FIRE_H / 2 + (sin8(tt + 64)  * FIRE_H) / 256;
    int cx2 = FIRE_W / 2 + (sin8(tt + 96)  * FIRE_W) / 256;
    int cy2 = FIRE_H / 2 + (sin8(tt + 160) * FIRE_H) / 256;
    for (int y = 0; y < FIRE_H; y++) {
        for (int x = 0; x < FIRE_W; x++) {
            int dx1 = x - cx1, dy1 = y - cy1;
            int dx2 = x - cx2, dy2 = y - cy2;
            int d1 = (dx1 * dx1 + dy1 * dy1) / 4;
            int d2 = (dx2 * dx2 + dy2 * dy2) / 4;
            int v = sin8(d1) + sin8(d2);   // -254..254
            int idx = ((v + 256) * FIRE_PAL) / 512;
            if (idx < 0) idx = 0;
            if (idx >= FIRE_PAL) idx = FIRE_PAL - 1;
            s_fireBuf[y * FIRE_W + x] = (uint8_t)idx;
        }
    }
}

void renderDemoscene(const Row& wRow, float secondOfMinuteF) {
    renderFire(wRow, secondOfMinuteF);
}

void renderFire(const Row& wRow, float secondOfMinuteF) {
    if (!dma) return;
    // Nyan es un efecto totalmente distinto (sprite + arcoiris, no usa el
    // buffer de paleta). Si esta seleccionado, delegamos.
    if (Config::cfg.demosceneEffect == 3) {
        renderNyan(wRow, secondOfMinuteF);
        return;
    }
    if (!s_fireInited) fireInit();
    initSinTable();
    // Re-genera la paleta si cambia la config (toggle clasica / color).
    static bool     s_lastUseDefault = true;
    static uint32_t s_lastColor      = 0xFF6000;
    if (Config::cfg.fireUseDefault != s_lastUseDefault ||
        (!Config::cfg.fireUseDefault && Config::cfg.fireColor != s_lastColor)) {
        if (Config::cfg.fireUseDefault) initFirePalette();
        else                            buildFirePaletteFromColor(Config::cfg.fireColor);
        s_lastUseDefault = Config::cfg.fireUseDefault;
        s_lastColor      = Config::cfg.fireColor;
    }
    uint8_t eff = Config::cfg.demosceneEffect;
    if (eff == 1)      plasmaStep(millis());
    else if (eff == 2) moireStep(millis());
    else               fireStep();
    dma->clearScreen();
    for (int y = 0; y < FIRE_H; y++) {
        for (int x = 0; x < FIRE_W; x++) {
            uint8_t idx = s_fireBuf[y * FIRE_W + x];
            if (idx == 0) continue;
            dma->drawPixel(x, y, s_firePal565[idx]);
        }
    }
    drawCommonBottomRow(wRow, secondOfMinuteF);
    drawActiveRipples();
    drawBrightnessOverlay();
    dma->flipDMABuffer();
}

// ── Modo Nyan Cat ──────────────────────────────────────────────────────
// Sprite 14x9 con cabeza, pop-tart y patitas. Bob vertical de 1 px.
// Estela arcoiris de 6 bandas detras del gato con efecto step (la trail
// se desplaza dando sensacion de movimiento).
//
// Indices del sprite:
//   0 = transparente, 1 = pink, 2 = sprinkle amarillo, 3 = gris, 4 = negro
// Sprite 12x12 fiel al GIF original: cabeza gris arriba-derecha con orejas,
// ojos y cheeks rosas, pop-tart con outline negro + borde peach + interior
// pink con sprinkles, 4 patitas.
//   1 = pink body fill #FFB8DC
//   2 = peach outline #FFC080
//   3 = sprinkle pink oscuro #E84A7A
//   4 = gray cat fur #C8C8C8
//   5 = black outline / ojo #000000
//   6 = pink cheek #FFA0C8
// Sprite del gato Nyan: 6 frames de 21 filas x 34 columnas, traducidos del
// GIF original (nyansmall.gif, 12 frames con ciclo de 6 unicos).
//
// Paleta (colores extraidos del GIF):
//   0 = transparente (skip)
//   1 = pink body fill   #FF99FF
//   2 = peach outline    #FFCC99
//   3 = sprinkle dark    #FF3399
//   4 = gray cat fur     #999999
//   5 = black            #000000
//   6 = cheek pink       #FF9999
//   7 = white            #FFFFFF
#define NYAN_NFRAMES 6
#define NYAN_H 21
#define NYAN_W 34
static const uint8_t NYAN_FRAMES[NYAN_NFRAMES][NYAN_H][NYAN_W] = {
    // F0
    {
        {0,0,0,0,0,0,0,0,0,5, 5,5,5,5,5,5,5,5,5,5, 5,5,5,5,5,5,0,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,0,5,2, 2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,5,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,2, 2,1,1,1,1,1,1,1,1,1, 1,1,1,1,2,2,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,2, 1,1,1,1,1,1,3,1,1,3, 1,1,1,1,1,2,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,3,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,1,5, 5,1,1,3,1,1,2,5,0,5, 5,0,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,5,4, 4,5,1,1,1,1,2,5,5,4, 4,5,0,0},
        {0,5,5,5,5,0,0,5,2,1, 1,1,1,1,1,3,1,1,5,4, 4,4,5,1,1,1,2,5,4,4, 4,5,0,0},
        {0,5,4,4,5,5,0,5,2,1, 1,1,1,1,1,1,1,1,5,4, 4,4,4,5,5,5,5,4,4,4, 4,5,0,0},
        {0,5,5,4,4,5,5,5,2,1, 1,1,3,1,1,1,1,1,5,4, 4,4,4,4,4,4,4,4,4,4, 4,5,0,0},
        {0,0,5,5,4,4,5,5,2,1, 1,1,1,1,1,1,3,5,4,4, 4,4,4,4,4,4,4,4,4,4, 4,4,5,0},
        {0,0,0,5,5,4,4,5,2,1, 3,1,1,1,1,1,1,5,4,4, 4,7,5,4,4,4,4,4,7,5, 4,4,5,0},
        {0,0,0,0,5,5,5,5,2,1, 1,1,1,1,1,1,1,5,4,4, 4,5,5,4,4,4,5,4,5,5, 4,4,5,0},
        {0,0,0,0,0,0,5,5,2,1, 1,1,1,1,3,1,1,5,4,6, 6,4,4,4,4,4,4,4,4,4, 6,6,5,0},
        {0,0,0,0,0,0,0,5,2,2, 1,3,1,1,1,1,1,5,4,6, 6,4,5,4,4,5,4,4,5,4, 6,6,5,0},
        {0,0,0,0,0,0,0,5,2,2, 2,1,1,1,1,1,1,1,5,4, 4,4,5,5,5,5,5,5,5,4, 4,5,0,0},
        {0,0,0,0,0,0,5,5,5,2, 2,2,2,2,2,2,2,2,2,5, 4,4,4,4,4,4,4,4,4,4, 5,0,0,0},
        {0,0,0,0,0,5,4,4,4,5, 5,5,5,5,5,5,5,5,5,5, 5,5,5,5,5,5,5,5,5,5, 0,0,0,0},
        {0,0,0,0,0,5,4,4,5,5, 0,5,4,4,5,0,0,0,0,0, 5,4,4,5,0,5,4,4,5,0, 0,0,0,0},
        {0,0,0,0,0,5,5,5,5,0, 0,5,5,5,0,0,0,0,0,0, 0,5,5,5,0,0,5,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0},
    },
    // F1
    {
        {0,0,0,0,0,0,0,0,0,5, 5,5,5,5,5,5,5,5,5,5, 5,5,5,5,5,5,0,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,0,5,2, 2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,5,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,2, 2,1,1,1,1,1,1,1,1,1, 1,1,1,1,2,2,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,2, 1,1,1,1,1,1,3,1,1,3, 1,1,1,1,1,2,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,3,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,1,1, 5,5,1,3,1,1,2,5,0,0, 5,5,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,1,5, 4,4,5,1,1,1,2,5,0,5, 4,4,5,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,3,1,1,1,5, 4,4,4,5,1,1,2,5,5,4, 4,4,5,0},
        {0,0,5,5,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,1,5, 4,4,4,4,5,5,5,5,4,4, 4,4,5,0},
        {0,5,4,4,5,0,0,5,2,1, 1,1,3,1,1,1,1,1,1,5, 4,4,4,4,4,4,4,4,4,4, 4,4,5,0},
        {0,5,4,4,5,5,5,5,2,1, 1,1,1,1,1,1,3,1,5,4, 4,4,4,4,4,4,4,4,4,4, 4,4,4,5},
        {0,0,5,4,4,4,4,5,2,1, 3,1,1,1,1,1,1,1,5,4, 4,4,7,5,4,4,4,4,4,7, 5,4,4,5},
        {0,0,0,5,5,4,4,5,2,1, 1,1,1,1,1,1,1,1,5,4, 4,4,5,5,4,4,4,5,4,5, 5,4,4,5},
        {0,0,0,0,0,5,5,5,2,1, 1,1,1,1,3,1,1,1,5,4, 6,6,4,4,4,4,4,4,4,4, 4,6,6,5},
        {0,0,0,0,0,0,0,5,2,2, 1,3,1,1,1,1,1,1,5,4, 6,6,4,5,4,4,5,4,4,5, 4,6,6,5},
        {0,0,0,0,0,0,0,5,2,2, 2,1,1,1,1,1,1,1,1,5, 4,4,4,5,5,5,5,5,5,5, 4,4,5,0},
        {0,0,0,0,0,0,0,5,5,2, 2,2,2,2,2,2,2,2,2,2, 5,4,4,4,4,4,4,4,4,4, 4,5,0,0},
        {0,0,0,0,0,0,5,4,4,5, 5,5,5,5,5,5,5,5,5,5, 5,5,5,5,5,5,5,5,5,5, 5,0,0,0},
        {0,0,0,0,0,0,5,4,4,5, 0,5,4,4,5,0,0,0,0,0, 0,5,4,4,5,0,5,4,4,5, 0,0,0,0},
        {0,0,0,0,0,0,5,5,5,0, 0,0,5,5,5,0,0,0,0,0, 0,0,5,5,5,0,0,5,5,5, 0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0},
    },
    // F2
    {
        {0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,0,0,5, 5,5,5,5,5,5,5,5,5,5, 5,5,5,5,5,5,0,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,0,5,2, 2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,5,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,2, 2,1,1,1,1,1,1,1,1,1, 1,1,1,1,2,2,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,2, 1,1,1,1,1,1,3,1,1,3, 1,1,1,1,1,2,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,3,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,1,1, 5,5,1,3,1,1,2,5,0,0, 5,5,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,1,5, 4,4,5,1,1,1,2,5,0,5, 4,4,5,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,3,1,1,1,5, 4,4,4,5,1,1,2,5,5,4, 4,4,5,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,1,5, 4,4,4,4,5,5,5,5,4,4, 4,4,5,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,3,1,1,1,1,1,1,5, 4,4,4,4,4,4,4,4,4,4, 4,4,5,0},
        {0,0,0,0,0,0,5,5,2,1, 1,1,1,1,1,1,3,1,5,4, 4,4,4,4,4,4,4,4,4,4, 4,4,4,5},
        {0,0,0,5,5,5,5,5,2,1, 3,1,1,1,1,1,1,1,5,4, 4,4,7,5,4,4,4,4,4,7, 5,4,4,5},
        {0,5,5,4,4,4,4,5,2,1, 1,1,1,1,1,1,1,1,5,4, 4,4,5,5,4,4,4,5,4,5, 5,4,4,5},
        {0,5,4,4,4,5,5,5,2,1, 1,1,1,1,3,1,1,1,5,4, 6,6,4,4,4,4,4,4,4,4, 4,6,6,5},
        {0,0,5,5,5,5,0,5,2,2, 1,3,1,1,1,1,1,1,5,4, 6,6,4,5,4,4,5,4,4,5, 4,6,6,5},
        {0,0,0,0,0,0,0,5,2,2, 2,1,1,1,1,1,1,1,1,5, 4,4,4,5,5,5,5,5,5,5, 4,4,5,0},
        {0,0,0,0,0,0,0,5,5,2, 2,2,2,2,2,2,2,2,2,2, 5,4,4,4,4,4,4,4,4,4, 4,5,0,0},
        {0,0,0,0,0,0,0,5,4,5, 5,5,5,5,5,5,5,5,5,5, 5,5,5,5,5,5,5,5,5,5, 5,0,0,0},
        {0,0,0,0,0,0,0,5,4,4, 5,0,5,4,4,5,0,0,0,0, 0,0,5,4,4,5,0,5,4,4, 5,0,0,0},
        {0,0,0,0,0,0,0,5,5,5, 0,0,0,5,5,5,0,0,0,0, 0,0,0,5,5,5,0,0,5,5, 5,0,0,0},
    },
    // F3
    {
        {0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,0,0,5, 5,5,5,5,5,5,5,5,5,5, 5,5,5,5,5,5,0,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,0,5,2, 2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,5,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,2, 2,1,1,1,1,1,1,1,1,1, 1,1,1,1,2,2,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,2, 1,1,1,1,1,1,3,1,1,3, 1,1,1,1,1,2,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,3,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,1,1, 5,5,1,3,1,1,2,5,0,0, 5,5,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,1,5, 4,4,5,1,1,1,2,5,0,5, 4,4,5,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,3,1,1,1,5, 4,4,4,5,1,1,2,5,5,4, 4,4,5,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,1,5, 4,4,4,4,5,5,5,5,4,4, 4,4,5,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,3,1,1,1,1,1,1,5, 4,4,4,4,4,4,4,4,4,4, 4,4,5,0},
        {0,0,0,0,0,5,5,5,2,1, 1,1,1,1,1,1,3,1,5,4, 4,4,4,4,4,4,4,4,4,4, 4,4,4,5},
        {0,0,0,5,5,4,4,5,2,1, 3,1,1,1,1,1,1,1,5,4, 4,4,7,5,4,4,4,4,4,7, 5,4,4,5},
        {0,0,5,4,4,4,4,5,2,1, 1,1,1,1,1,1,1,1,5,4, 4,4,5,5,4,4,4,5,4,5, 5,4,4,5},
        {0,5,4,4,5,5,5,5,2,1, 1,1,1,1,3,1,1,1,5,4, 6,6,4,4,4,4,4,4,4,4, 4,6,6,5},
        {0,5,4,4,5,0,0,5,2,2, 1,3,1,1,1,1,1,1,5,4, 6,6,4,5,4,4,5,4,4,5, 4,6,6,5},
        {0,0,5,5,0,0,0,5,2,2, 2,1,1,1,1,1,1,1,1,5, 4,4,4,5,5,5,5,5,5,5, 4,4,5,0},
        {0,0,0,0,0,0,0,5,5,2, 2,2,2,2,2,2,2,2,2,2, 5,4,4,4,4,4,4,4,4,4, 4,5,0,0},
        {0,0,0,0,0,0,5,4,4,5, 5,5,5,5,5,5,5,5,5,5, 5,5,5,5,5,5,5,5,5,5, 5,0,0,0},
        {0,0,0,0,0,0,5,4,4,5, 0,5,4,4,5,0,0,0,0,0, 0,5,4,4,5,0,5,4,4,5, 0,0,0,0},
        {0,0,0,0,0,0,5,5,5,0, 0,0,5,5,5,0,0,0,0,0, 0,0,5,5,5,0,0,5,5,5, 0,0,0,0},
    },
    // F4
    {
        {0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,0,0,5, 5,5,5,5,5,5,5,5,5,5, 5,5,5,5,5,5,0,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,0,5,2, 2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,5,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,2, 2,1,1,1,1,1,1,1,1,1, 1,1,1,1,2,2,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,2, 1,1,1,1,1,1,3,1,1,3, 1,1,1,1,1,2,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,3,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,1,5, 5,1,1,3,1,1,2,5,0,5, 5,0,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,5,4, 4,5,1,1,1,1,2,5,5,4, 4,5,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,3,1,1,5,4, 4,4,5,1,1,1,2,5,4,4, 4,5,0,0},
        {0,5,5,5,5,0,0,5,2,1, 1,1,1,1,1,1,1,1,5,4, 4,4,4,5,5,5,5,4,4,4, 4,5,0,0},
        {5,4,4,4,5,5,5,5,2,1, 1,1,3,1,1,1,1,1,5,4, 4,4,4,4,4,4,4,4,4,4, 4,5,0,0},
        {5,5,4,4,4,4,5,5,2,1, 1,1,1,1,1,1,3,5,4,4, 4,4,4,4,4,4,4,4,4,4, 4,4,5,0},
        {0,0,5,5,5,5,4,5,2,1, 3,1,1,1,1,1,1,5,4,4, 4,7,5,4,4,4,4,4,7,5, 4,4,5,0},
        {0,0,0,0,0,5,5,5,2,1, 1,1,1,1,1,1,1,5,4,4, 4,5,5,4,4,4,5,4,5,5, 4,4,5,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,3,1,1,5,4,6, 6,4,4,4,4,4,4,4,4,4, 6,6,5,0},
        {0,0,0,0,0,0,0,5,2,2, 1,3,1,1,1,1,1,5,4,6, 6,4,5,4,4,5,4,4,5,4, 6,6,5,0},
        {0,0,0,0,0,0,5,5,2,2, 2,1,1,1,1,1,1,1,5,4, 4,4,5,5,5,5,5,5,5,4, 4,5,0,0},
        {0,0,0,0,0,5,5,5,5,2, 2,2,2,2,2,2,2,2,2,5, 4,4,4,4,4,4,4,4,4,4, 5,0,0,0},
        {0,0,0,0,5,4,4,4,5,5, 5,5,5,5,5,5,5,5,5,5, 5,5,5,5,5,5,5,5,5,5, 0,0,0,0},
        {0,0,0,0,5,4,4,5,0,5, 4,4,5,0,0,0,0,0,0,5, 4,4,5,0,5,4,4,5,0,0, 0,0,0,0},
        {0,0,0,0,5,5,5,0,0,0, 5,5,5,0,0,0,0,0,0,0, 5,5,5,0,0,5,5,5,0,0, 0,0,0,0},
    },
    // F5
    {
        {0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,0,0,5, 5,5,5,5,5,5,5,5,5,5, 5,5,5,5,5,5,0,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,0,5,2, 2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,5,0,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,2, 2,1,1,1,1,1,1,1,1,1, 1,1,1,1,2,2,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,2, 1,1,1,1,1,1,3,1,1,3, 1,1,1,1,1,2,2,5,0,0, 0,0,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,3,1,1,1,1,1,1,1,5, 5,1,1,1,1,1,2,5,0,5, 5,0,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,5,4, 4,5,1,3,1,1,2,5,5,4, 4,5,0,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,1,1,1,1,5,4, 4,4,5,1,1,1,2,5,4,4, 4,5,0,0},
        {0,0,5,5,0,0,0,5,2,1, 1,1,1,1,1,3,1,1,5,4, 4,4,4,5,5,5,5,4,4,4, 4,5,0,0},
        {0,5,4,4,5,0,0,5,2,1, 1,1,1,1,1,1,1,1,5,4, 4,4,4,4,4,4,4,4,4,4, 4,5,0,0},
        {0,5,4,4,5,5,5,5,2,1, 1,1,3,1,1,1,1,5,4,4, 4,4,4,4,4,4,4,4,4,4, 4,4,5,0},
        {0,0,5,4,4,4,4,5,2,1, 1,1,1,1,1,1,3,5,4,4, 4,7,5,4,4,4,4,4,7,5, 4,4,5,0},
        {0,0,0,5,5,4,4,5,2,1, 3,1,1,1,1,1,1,5,4,4, 4,5,5,4,4,4,5,4,5,5, 4,4,5,0},
        {0,0,0,0,0,5,5,5,2,1, 1,1,1,1,1,1,1,5,4,6, 6,4,4,4,4,4,4,4,4,4, 6,6,5,0},
        {0,0,0,0,0,0,0,5,2,1, 1,1,1,1,3,1,1,5,4,6, 6,4,5,4,4,5,4,4,5,4, 6,6,5,0},
        {0,0,0,0,0,0,0,5,2,2, 1,3,1,1,1,1,1,1,5,4, 4,4,5,5,5,5,5,5,5,4, 4,5,0,0},
        {0,0,0,0,0,0,5,5,2,2, 2,1,1,1,1,1,1,1,1,5, 4,4,4,4,4,4,4,4,4,4, 5,0,0,0},
        {0,0,0,0,0,5,4,5,5,2, 2,2,2,2,2,2,2,2,2,2, 5,5,5,5,5,5,5,5,5,5, 0,0,0,0},
        {0,0,0,0,5,4,4,4,5,5, 5,5,5,5,5,5,5,5,5,5, 5,5,5,5,5,5,4,5,0,0, 0,0,0,0},
        {0,0,0,0,5,4,4,5,0,5, 4,4,5,0,0,0,0,0,0,5, 4,4,5,0,5,4,4,5,0,0, 0,0,0,0},
        {0,0,0,0,5,5,5,0,0,5, 5,5,0,0,0,0,0,0,0,5, 5,5,0,0,0,5,5,5,0,0, 0,0,0,0},
    },
};

static uint16_t s_nyanPal[8] = {0, 0, 0, 0, 0, 0, 0, 0};
static uint16_t s_nyanRainbow[6] = {0, 0, 0, 0, 0, 0};
static bool s_nyanInited = false;

// Starfield con parallax: 10 estrellas distribuidas en 3 niveles de
// profundidad (0=lejos/lento/oscuro, 2=cerca/rapido/claro). Y, depth y
// offset inicial precomputados para que parezca aleatorio pero sea estable.
static const int8_t  STAR_Y[10]      = { 2, 17,  9, 21,  5, 13, 19,  1, 11,  7};
static const uint8_t STAR_DEPTH[10]  = { 0,  2,  1,  0,  2,  1,  0,  1,  2,  0};
static const int8_t  STAR_OFFSET[10] = { 0, 32, 13, 47, 23, 51,  7, 38, 18, 56};
// ms por pixel — mas alto = mas lento (mas lejos)
static const uint16_t STAR_SPEED_MS[3] = {280, 150, 80};
static uint16_t s_starColor[3] = {0, 0, 0};

static void initNyanPal() {
    s_nyanPal[0] = 0;
    s_nyanPal[1] = rgb888to565(0xFF99FF);   // pink body fill (del GIF)
    s_nyanPal[2] = rgb888to565(0xFFCC99);   // peach outline (del GIF)
    s_nyanPal[3] = rgb888to565(0xFF3399);   // sprinkle dark pink (del GIF)
    s_nyanPal[4] = rgb888to565(0x999999);   // gray cat fur (del GIF)
    s_nyanPal[5] = rgb888to565(0x000000);   // black
    s_nyanPal[6] = rgb888to565(0xFF9999);   // cheek pink (del GIF)
    s_nyanPal[7] = rgb888to565(0xFFFFFF);   // white (highlights ojos)
    s_nyanRainbow[0] = rgb888to565(0xFF0000);
    s_nyanRainbow[1] = rgb888to565(0xFF8800);
    s_nyanRainbow[2] = rgb888to565(0xFFE600);
    s_nyanRainbow[3] = rgb888to565(0x00C800);
    s_nyanRainbow[4] = rgb888to565(0x00A0FF);
    s_nyanRainbow[5] = rgb888to565(0xC800FF);
    // Colores del starfield: tonos azulados, mas brillantes cuanto mas cerca,
    // sin saturar para no destacar excesivamente sobre el fondo #000033.
    s_starColor[0] = rgb888to565(0x1F1F70);   // lejos: azul oscuro
    s_starColor[1] = rgb888to565(0x4040A0);   // mid:   azul medio
    s_starColor[2] = rgb888to565(0x80A0E0);   // cerca: azul claro
    s_nyanInited = true;
}

void renderNyan(const Row& wRow, float secondOfMinuteF) {
    if (!dma) return;
    if (!s_nyanInited) initNyanPal();
    uint32_t now = millis();

    // Limpia toda la pantalla (incluido el area inferior del reloj) — si no,
    // los pixels del ripple del boton dejan rastro en y>=FIRE_H entre frames.
    dma->clearScreen();

    // Fondo azul oscuro estilo "espacio" en la zona superior (encima de la
    // fila inferior, que se redibuja con drawCommonBottomRow).
    static const uint16_t BG_COLOR = rgb888to565(0x000033);
    for (int y = 0; y < (int)FIRE_H; y++) {
        for (int x = 0; x < WIDTH; x++) {
            dma->drawPixel(x, y, BG_COLOR);
        }
    }

    // Starfield con parallax: 10 estrellas en 3 capas de profundidad
    // moviendose de derecha a izquierda. Se pinta ANTES del arcoiris y el
    // gato para que ambos queden por delante.
    for (int i = 0; i < 10; i++) {
        uint8_t depth = STAR_DEPTH[i];
        int shift = (int)(now / STAR_SPEED_MS[depth]);
        int sx = ((int)STAR_OFFSET[i] - shift) % WIDTH;
        if (sx < 0) sx += WIDTH;
        int sy = STAR_Y[i];
        if (sy >= 0 && sy < (int)FIRE_H) {
            dma->drawPixel(sx, sy, s_starColor[depth]);
        }
    }

    // Sprite 34x21, 6 frames de animacion. Anclado a la derecha del panel.
    // El bouncing vertical ya esta incluido dentro de los frames del GIF.
    const int catW = NYAN_W;                  // 34
    const int catH = NYAN_H;                  // 21
    const int catXpos = WIDTH - catW - 1;     // x=29
    int catY = (FIRE_H - catH) / 2;           // centrado vertical fijo

    // Frame de animacion: 6 frames a 70ms (timing del GIF original) = 420ms ciclo.
    int frameIdx = (now / 70) % NYAN_NFRAMES;

    // Estela arcoiris: 6 bandas, 2 px de alto cada una = 12 px total.
    // Se dibuja hasta dentro del cuerpo del gato (hasta catXpos + 26, que es
    // el borde derecho del pop-tart) y luego el sprite del gato la tapa por
    // encima. Asi el arcoiris parece salir desde detras del gato.
    const int trailOffset = catH / 2 - 6;     // ~6 px sobre el centro vertical
    int trailY = catY + trailOffset;
    int tShift = (int)(now / 60);
    const int trailXmax = catXpos + 26;       // hasta el borde derecho del body
    for (int x = 0; x < trailXmax && x < WIDTH; x++) {
        int step = (((x + tShift) / 4) & 1);
        for (int b = 0; b < 6; b++) {
            int py0 = trailY + (b * 2) + step;
            int py1 = py0 + 1;
            if (py0 >= 0 && py0 < (int)FIRE_H) dma->drawPixel(x, py0, s_nyanRainbow[b]);
            if (py1 >= 0 && py1 < (int)FIRE_H) dma->drawPixel(x, py1, s_nyanRainbow[b]);
        }
    }

    // Sprite del gato (frame actual) encima del fondo y la estela.
    for (int sy = 0; sy < catH; sy++) {
        for (int sx = 0; sx < catW; sx++) {
            uint8_t idx = NYAN_FRAMES[frameIdx][sy][sx];
            if (idx == 0) continue;
            int px = catXpos + sx;
            int py = catY + sy;
            if (px >= 0 && px < WIDTH && py >= 0 && py < (int)FIRE_H) {
                dma->drawPixel(px, py, s_nyanPal[idx]);
            }
        }
    }

    drawCommonBottomRow(wRow, secondOfMinuteF);
    drawActiveRipples();
    drawBrightnessOverlay();
    dma->flipDMABuffer();
}

}  // namespace Display
