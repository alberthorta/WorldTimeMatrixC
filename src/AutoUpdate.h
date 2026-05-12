// Auto-update via GitHub Releases. En cada boot, tras WiFi STA, consulta la
// release mas reciente del repo; si su tag_name difiere de FW_VERSION, descarga
// el .bin adjunto y flashea via Update.
//
// Requiere que las releases del repo sean publicas (el repo en si puede ser
// privado). El asset debe llamarse `firmware.bin` o terminar en `.bin`.
#pragma once

#include <Arduino.h>

namespace AutoUpdate {

struct ReleaseInfo {
    bool found = false;            // false en cualquier error (404, sin red, etc.)
    String tagName;                // p.ej. "v0.2.0"
    String binUrl;                 // primer asset que termina en .bin
};

// Pull no destructivo: GET a /releases/latest, parsea tag_name y assets[].
// Timeout total ~15s. Devuelve found=false si: no hay releases, no hay asset
// .bin, JSON malformado, o cualquier error de red/HTTPS.
ReleaseInfo fetchLatestRelease();

// Descarga el .bin desde `url` (sigue redirects de github.com -> CDN) y lo
// escribe en la particion OTA inactiva via Update.h. Llama a `progress(pct)`
// con porcentaje 0..100 cuando cambia.
// Devuelve true si Update.end() exitoso — en ese caso el caller deberia
// ESP.restart() para arrancar el firmware nuevo.
bool downloadAndFlash(const String& url, void (*progress)(int) = nullptr);

}  // namespace AutoUpdate
