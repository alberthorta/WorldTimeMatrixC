#include "Display.h"

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Fonts/TomThumb.h>
#include <math.h>

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

    for (int i = 0; i < 4; i++) {
        const Row& r = rows[i];
        int y = ROW_YS[i];

        // Nombre a la izquierda.
        dma->setTextColor(r.color);
        dma->setCursor(NAME_X, y);
        dma->print(r.name);

        if (r.hasTime) {
            char hh[3], mm[3];
            // Formato hora: con cero a la izquierda siempre (default) o sin
            // el si Config::cfg.hourLeadingZero es false y la hora < 10.
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
                // Fade del ":" via escalado RGB. Decode 565→888, escala por
                // alpha, encode→565. Para alpha≈1 saltamos la conversion para
                // ahorrar la perdida de precision por redondeo.
                uint16_t colonColor = r.color;
                if (r.colonAlpha < 0.999f) {
                    uint8_t R5 = (r.color >> 11) & 0x1F;
                    uint8_t G6 = (r.color >> 5)  & 0x3F;
                    uint8_t B5 =  r.color        & 0x1F;
                    uint8_t R = (R5 << 3) | (R5 >> 2);
                    uint8_t G = (G6 << 2) | (G6 >> 4);
                    uint8_t B = (B5 << 3) | (B5 >> 2);
                    R = (uint8_t)((float)R * r.colonAlpha + 0.5f);
                    G = (uint8_t)((float)G * r.colonAlpha + 0.5f);
                    B = (uint8_t)((float)B * r.colonAlpha + 0.5f);
                    colonColor = rgb888to565(((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B);
                }
                dma->setTextColor(colonColor);
                dma->setCursor(xCur, y + COLON_Y_OFFSET);
                dma->print(":");
                // Restaura el color original para los digitos del minuto.
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
    dma->flipDMABuffer();   // intercambia front/back, anti-flicker
}

}  // namespace Display
