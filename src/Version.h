// FW_VERSION lo inyecta scripts/version.py (via -DFW_VERSION=...) en cada build.
// Fallback "unknown" si por alguna razon el build no pasa por el script.
#pragma once

#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif
