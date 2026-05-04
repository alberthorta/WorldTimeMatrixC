// HTML embebido como fallback. Si LittleFS no tiene /index.html (primer boot,
// uploadfs roto, etc.) lo seedea desde aqui.
#pragma once

#include <pgmspace.h>
#include <stddef.h>

extern const char INDEX_HTML[] PROGMEM;
extern const size_t INDEX_HTML_LEN;
