// Jitter — raton BLE HID anti-inactividad + servicio de control BLE.
//
// Portado del proyecto gizmo (../gizmo/tablet/main/jitter.c, ESP-IDF + esp_hid
// sobre NimBLE) a Arduino/NimBLE-Arduino. El device se anuncia como raton BLE
// ("WorldTime Jitter"); macOS lo empareja como raton y, si el jitter esta
// activo, mueve el cursor un poco cada intervalo (direccion y distancia
// aleatorias, con recentrado suave) para evitar que el equipo entre en reposo.
//
// Ademas expone el MISMO servicio de control 6B1D0001-... que la app de
// escritorio original, con el mismo protocolo de bytes, de modo que la app
// macOS (mac/) puede arrancar/parar el jitter y ajustar intervalo + paso.
//
// El estado (enabled / intervalMs / maxStep) vive en Config::cfg y se persiste
// en /cfg.json (LittleFS), asi que se puede tocar indistintamente por BLE o
// por la web UI (/api/config), y al reboot se recuerda: si estaba activo y hay
// un host BLE emparejado, el cursor vuelve a moverse solo.
#pragma once

#include <stdbool.h>
#include <stdint.h>

namespace Jitter {

// Inicializa NimBLE + raton HID + servicio de control y arranca la task del
// motor. Debe llamarse UNA sola vez, despues de Config::begin() (lee la config)
// y preferiblemente con WiFi ya levantada (coexistencia software del ESP32-S3).
void begin();

// True si hay un host BLE (p.ej. el Mac) conectado al raton.
bool hostConnected();

// Reenvia el estado actual (enabled/interval/step) a los suscriptores del
// status characteristic. El motor ya lo llama solo cuando detecta cambios de
// config (incluidos los que llegan por la web); expuesto por si se quiere
// forzar desde fuera.
void notifyStatus();

}  // namespace Jitter
