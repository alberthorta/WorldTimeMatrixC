#include "Icons.h"

#include <string.h>

#include "Config.h"
#include "Display.h"

namespace Icons {

IconDef icons[NUM_ICONS];

const uint32_t DEFAULT_PALETTE[PALETTE_SIZE] = {
    0x000000, 0xFFDC00, 0x969696, 0x64A0FF, 0xFFFFFF, 0x648CC8,
    0xFF3B30, 0xFF9500, 0x34C759, 0x00BCD4, 0xFF2D92, 0x4B5563,
    0x8B5A3C, 0xAF52DE, 0xC8C8C8, 0x5856D6,
};

static const uint8_t DEF_PX[NUM_ICONS][ICON_H][ICON_W] = {
    {{0,1,0,1,0},{1,1,1,1,1},{0,1,1,1,0},{1,1,1,1,1},{0,1,0,1,0}},      // SUN
    {{0,1,0,0,0},{1,1,1,0,0},{0,1,2,2,0},{0,2,2,2,2},{0,0,0,0,0}},      // PARTLY
    {{0,0,0,0,0},{0,2,2,0,0},{2,2,2,2,0},{0,2,2,2,2},{0,0,0,0,0}},      // CLOUD
    {{0,2,2,2,0},{2,2,2,2,2},{0,0,0,0,0},{3,0,3,0,3},{0,3,0,3,0}},      // RAIN
    {{0,2,2,2,0},{2,2,2,2,2},{0,0,0,0,0},{4,0,4,0,4},{0,4,0,4,0}},      // SNOW
    {{0,2,2,2,0},{2,2,2,2,2},{0,0,1,0,0},{0,1,1,0,0},{0,0,1,0,0}},      // STORM
    {{0,0,0,0,0},{2,2,2,2,2},{0,0,0,0,0},{2,2,2,2,2},{0,0,0,0,0}},      // FOG
    // MOON base (fallback si la variante de fase no esta definida): shape "(".
    // Por compat retro queda igual a antes; en runtime se sustituye por la
    // variante de fase actual (MOON_NEW/WAXING/FULL/WANING).
    {{0,5,5,0,0},{5,5,0,0,0},{5,5,0,0,0},{5,5,0,0,0},{0,5,5,0,0}},      // MOON
    {{0,5,0,0,0},{5,5,0,0,0},{0,5,2,2,0},{0,2,2,2,2},{0,0,0,0,0}},      // PARTLY_NIGHT
    // Variantes de fase lunar para MOON. Color idx 5 = azul tenue (luna),
    // idx 11 = gris oscuro (luna nueva = solo silueta).
    {{0,11,11,11,0},{11,0,0,0,11},{11,0,0,0,11},{11,0,0,0,11},{0,11,11,11,0}}, // MOON_NEW
    {{0,0,5,5,0},{0,0,0,5,5},{0,0,0,5,5},{0,0,0,5,5},{0,0,5,5,0}},      // MOON_WAXING
    {{0,5,5,5,0},{5,5,5,5,5},{5,5,5,5,5},{5,5,5,5,5},{0,5,5,5,0}},      // MOON_FULL
    {{0,5,5,0,0},{5,5,0,0,0},{5,5,0,0,0},{5,5,0,0,0},{0,5,5,0,0}},      // MOON_WANING
    // Variantes de fase lunar para PARTLY_NIGHT (luna+nube). El espacio es muy
    // limitado en 5x5 con la nube ocupando la esquina inferior derecha, asi que
    // los matices son sutiles. El usuario puede editarlos via UI.
    {{0,11,0,0,0},{11,0,0,0,0},{0,11,2,2,0},{0,2,2,2,2},{0,0,0,0,0}},   // PARTLY_NIGHT_NEW
    {{0,5,5,0,0},{5,5,5,0,0},{0,5,2,2,0},{0,2,2,2,2},{0,0,0,0,0}},      // PARTLY_NIGHT_WAXING
    {{5,5,5,0,0},{5,5,5,0,0},{5,5,2,2,0},{0,2,2,2,2},{0,0,0,0,0}},      // PARTLY_NIGHT_FULL
    {{0,5,0,0,0},{5,5,0,0,0},{0,5,2,2,0},{0,2,2,2,2},{0,0,0,0,0}},      // PARTLY_NIGHT_WANING
};

static const char* DEF_NAMES[NUM_ICONS] = {
    "SUN", "PARTLY", "CLOUD", "RAIN", "SNOW", "STORM", "FOG", "MOON", "PARTLY_NIGHT",
    "MOON_NEW", "MOON_WAXING", "MOON_FULL", "MOON_WANING",
    "PARTLY_NIGHT_NEW", "PARTLY_NIGHT_WAXING", "PARTLY_NIGHT_FULL", "PARTLY_NIGHT_WANING"
};

void begin() {
    for (int i = 0; i < NUM_ICONS; i++) {
        icons[i].name = DEF_NAMES[i];
        Frame f;
        memcpy(f.px, DEF_PX[i], sizeof(f.px));
        f.ms = 500;
        icons[i].frames.clear();
        icons[i].frames.push_back(f);
    }
}

const IconDef* find(const char* name) {
    if (!name) return nullptr;
    for (int i = 0; i < NUM_ICONS; i++) {
        if (icons[i].name == name) return &icons[i];
    }
    return nullptr;
}

int indexFromName(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < NUM_ICONS; i++) {
        if (icons[i].name == name) return i;
    }
    return -1;
}

uint16_t paletteAs565(int idx) {
    if (idx < 0 || idx >= PALETTE_SIZE) return 0;
    return Display::rgb888to565(Config::cfg.palette[idx]);
}

void serializeAll(JsonObject obj) {
    for (int i = 0; i < NUM_ICONS; i++) {
        JsonArray frames = obj[icons[i].name].to<JsonArray>();
        for (const auto& f : icons[i].frames) {
            JsonObject fo = frames.add<JsonObject>();
            JsonArray rows = fo["px"].to<JsonArray>();
            for (int y = 0; y < ICON_H; y++) {
                JsonArray row = rows.add<JsonArray>();
                for (int x = 0; x < ICON_W; x++) row.add(f.px[y][x]);
            }
            fo["ms"] = f.ms;
        }
    }
}

void deserializeAll(JsonObjectConst obj) {
    for (int i = 0; i < NUM_ICONS; i++) {
        JsonArrayConst frames = obj[icons[i].name];
        if (frames.isNull()) continue;
        std::vector<Frame> newFrames;
        for (JsonVariantConst v : frames) {
            Frame f;
            memset(f.px, 0, sizeof(f.px));
            JsonArrayConst rows = v["px"];
            for (int y = 0; y < ICON_H && y < (int)rows.size(); y++) {
                JsonArrayConst row = rows[y];
                for (int x = 0; x < ICON_W && x < (int)row.size(); x++) {
                    int idx = row[x] | 0;
                    if (idx < 0) idx = 0;
                    if (idx >= PALETTE_SIZE) idx = 0;
                    f.px[y][x] = (uint8_t)idx;
                }
            }
            f.ms = v["ms"] | 500;
            if (f.ms < 50) f.ms = 50;
            newFrames.push_back(f);
        }
        if (!newFrames.empty()) icons[i].frames = std::move(newFrames);
    }
}

}  // namespace Icons
