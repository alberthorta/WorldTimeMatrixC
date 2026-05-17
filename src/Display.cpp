#include "Display.h"

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Fonts/TomThumb.h>
#include <Fonts/FreeSans12pt7b.h>
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
        char hhmm[8];
        bool leadingZero = Config::cfg.hourLeadingZero || r.hour >= 10;
        if (leadingZero) snprintf(hhmm, sizeof(hhmm), "%02u:%02u", r.hour, r.minute);
        else             snprintf(hhmm, sizeof(hhmm), "%u:%02u",  r.hour, r.minute);
        dma->setFont(&FreeSans12pt7b);
        dma->setTextSize(1);
        dma->setTextColor(rgb888to565(Config::cfg.focusHourColor));
        int16_t x1, y1; uint16_t w, h;
        dma->getTextBounds(hhmm, 0, 0, &x1, &y1, &w, &h);
        int x = (WIDTH - (int)w) / 2 - (int)x1;   // centrado horizontal
        int y = 1 - (int)y1;                       // top en y=1
        dma->setCursor(x, y);
        dma->print(hhmm);
        hourBottomY = 1 + (int)h - 1;             // top + h - 1
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

    // ---- 4) Segundera: barra horizontal de 2px de alto en y=30..31. ----
    if (secondOfMinuteF >= 0.0f && secondOfMinuteF < 60.0f) {
        int barEnd = (int)(secondOfMinuteF * (float)WIDTH / 60.0f + 0.5f);
        if (barEnd > WIDTH) barEnd = WIDTH;
        uint16_t dim = rgb888to565(0x303030);
        uint16_t head = rgb888to565(0xC0C0C0);
        for (int x = 0; x < barEnd; x++) {
            dma->drawPixel(x, 30, dim);
            dma->drawPixel(x, 31, dim);
        }
        if (barEnd > 0 && barEnd <= WIDTH) {
            dma->drawPixel(barEnd - 1, 30, head);
            dma->drawPixel(barEnd - 1, 31, head);
        }
    }

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
static void drawClaudePaceBar(int y0, int height, double used, double elapsed,
                              uint32_t color888) {
    uint16_t bg     = rgb888to565(0x202020);
    uint16_t fill   = rgb888to565(color888);
    uint16_t marker = rgb888to565(0xFFFFFF);
    if (used < 0) used = 0; if (used > 1) used = 1;
    if (elapsed < 0) elapsed = 0; if (elapsed > 1) elapsed = 1;
    int fillW = (int)(used * WIDTH + 0.5);
    int mx    = (int)(elapsed * WIDTH + 0.5);
    if (mx >= WIDTH) mx = WIDTH - 1;
    for (int x = 0; x < WIDTH; x++) {
        uint16_t c = (x < fillW) ? fill : bg;
        for (int dy = 0; dy < height; dy++) dma->drawPixel(x, y0 + dy, c);
    }
    if (mx >= 0) {
        for (int dy = 0; dy < height; dy++) dma->drawPixel(mx, y0 + dy, marker);
    }
}

void renderClaude(const Row& weatherRow, const ClaudeView& cv, float secondOfMinuteF) {
    if (!dma) return;
    dma->clearScreen();
    dma->setFont(&TomThumb);
    dma->setTextSize(1);

    uint16_t white = rgb888to565(0xFFFFFF);
    uint16_t dim   = rgb888to565(0xAAAAAA);
    uint16_t dim2  = rgb888to565(0x666666);

    // ── Header: HH:MM:SS | icono animado | TT° (info de cities[0]) ──
    // HH:MM:SS ocupa ~32 px (8 chars TomThumb), por lo que dejamos el icono
    // pegado a su derecha en lugar de centrarlo.
    if (weatherRow.hasTime) {
        char hms[12];
        int sec = (secondOfMinuteF >= 0.0f) ? (int)secondOfMinuteF : 0;
        if (sec < 0) sec = 0; if (sec > 59) sec = 59;
        bool lz = Config::cfg.hourLeadingZero || weatherRow.hour >= 10;
        if (lz) snprintf(hms, sizeof(hms), "%02u:%02u:%02d",
                         weatherRow.hour, weatherRow.minute, sec);
        else    snprintf(hms, sizeof(hms), "%u:%02u:%02d",
                         weatherRow.hour, weatherRow.minute, sec);
        dma->setTextColor(white);
        dma->setCursor(0, 5);
        dma->print(hms);
    }
    if (weatherRow.hasWeather && weatherRow.icon != IconType::NONE) {
        const Icons::Frame* frame = nullptr;
        if (g_previewActive) frame = tickPreviewAnim();
        else                 frame = tickRowAnim(0, weatherRow.icon);
        // Tras la hora HH:MM:SS (~x=32). Pongo el icono en x=35 con 1px de
        // margen, dejando hueco para la temp en la derecha.
        if (frame) drawFrame(35, 0, *frame);
    }
    if (weatherRow.hasWeather) {
        char tnum[6];
        snprintf(tnum, sizeof(tnum), "%d", weatherRow.tempC);
        uint16_t tc = tempColor(weatherRow.tempC);
        int16_t x1, y1; uint16_t w, h;
        dma->getTextBounds(tnum, 0, 0, &x1, &y1, &w, &h);
        const int DEG_W = 2, DEG_GAP = 1;
        int totalW = (int)w + DEG_GAP + DEG_W;
        int xStart = WIDTH - totalW;
        dma->setTextColor(tc);
        dma->setCursor(xStart, 5);
        dma->print(tnum);
        int degX = xStart + (int)w + DEG_GAP;
        int degTopY = 0;
        for (int yy = 0; yy < 2; yy++)
            for (int xx = 0; xx < 2; xx++)
                dma->drawPixel(degX + xx, degTopY + yy, tc);
    }

    // ── Linea 5h ──
    if (cv.fiveValid) {
        char line[24];
        long mins = cv.fiveRemainingSec / 60;
        snprintf(line, sizeof(line), "5h %d%% %ldh%02ldm",
                 (int)(cv.fiveUsed * 100.0 + 0.5),
                 mins / 60, mins % 60);
        dma->setTextColor(dim);
        dma->setCursor(0, 11);
        dma->print(line);
        drawClaudePaceBar(11, 4, cv.fiveUsed, cv.fiveElapsed, cv.fiveColor);
    } else {
        dma->setTextColor(dim2);
        dma->setCursor(0, 11);
        dma->print(cv.hasData ? "5h: no data" : "fetching 5h...");
    }

    // ── Linea 7d ──
    if (cv.sevenValid) {
        char line[24];
        long days  = cv.sevenRemainingSec / 86400;
        long hours = (cv.sevenRemainingSec % 86400) / 3600;
        snprintf(line, sizeof(line), "7d %d%% %ldd%ldh",
                 (int)(cv.sevenUsed * 100.0 + 0.5), days, hours);
        dma->setTextColor(dim);
        dma->setCursor(0, 22);
        dma->print(line);
        drawClaudePaceBar(22, 4, cv.sevenUsed, cv.sevenElapsed, cv.sevenColor);
    } else {
        dma->setTextColor(dim2);
        dma->setCursor(0, 22);
        dma->print(cv.hasData ? "7d: no data" : "fetching 7d...");
    }

    // ── Pace label 5h en y=29..31 (5px de alto, baseline 31) ──
    if (cv.fiveValid && cv.fiveLabel && *cv.fiveLabel) {
        dma->setTextColor(rgb888to565(cv.fiveColor));
        int16_t x1, y1; uint16_t w, h;
        dma->getTextBounds(cv.fiveLabel, 0, 0, &x1, &y1, &w, &h);
        int x = (WIDTH - (int)w) / 2 - (int)x1;
        dma->setCursor(x, 31);
        dma->print(cv.fiveLabel);
    }

    drawActiveRipples();
    drawBrightnessOverlay();
    dma->flipDMABuffer();
}

}  // namespace Display
