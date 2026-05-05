# MATRIXPORTAL.md — Notas de hardware (Matrix Portal S3 + HUB75)

Aprendizajes acumulados trabajando con esta placa. Pensado para reusar como referencia en otros proyectos sobre el mismo hardware.

## Hardware

- **Placa**: [Adafruit Matrix Portal S3](https://www.adafruit.com/product/5778)
  - SoC: ESP32-S3 (xtensa, dual core, 240MHz)
  - Flash: 8MB
  - PSRAM: 2MB (octal SPI)
  - USB-C nativo, conexión USB CDC para serial / upload
  - Botones físicos: `BOOT` (GPIO0) y `RESET`
  - LED RGB neopixel + 2 botones UP/DOWN
- **Panel**: HUB75 P5 64×32 px, 1/16 scan, driver típico Epstar/Silan
- **Fuente**: 5V externa al cable de entrada del panel. **NO** alimentar el panel desde el USB-C del Matrix Portal — el regulador de la placa no aguanta los picos del panel a brillo medio/alto.

## GPIO mapping HUB75 (Matrix Portal S3)

```
R1=42  G1=41  B1=40        (top half)
R2=38  G2=39  B2=37        (bottom half)
A=45   B=36   C=48   D=35  E=-1
LAT=47  OE=14  CLK=2
```

Estos son los pines que la placa tiene **fijos por hardware** al conector HUB75 hembra. Cualquier librería HUB75 sobre esta placa debe usar exactamente estos.

## Gotcha #1: orden RGB del panel concreto

Los paneles HUB75 económicos a veces cablean los canales **G y B intercambiados** respecto al estándar. Síntoma: los rojos se ven bien, pero los verdes salen azules y viceversa.

Hay que probar con el panel concreto: si una imagen "azul cielo" sale verdosa, el panel necesita **RBG** en lugar de **RGB**. La librería [`ESP32-HUB75-MatrixPanel-DMA`](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA) acepta `driver` y un swap manual de canales — alternativamente, swappear en el HUB75_I2S_CFG la asignación de pines G1↔B1 y G2↔B2.

Recomendación: hacer este orden **configurable** en runtime (NVS), no hardcoded — devices distintos pueden tener paneles distintos. Y al cambiarlo **requiere reboot** porque la lib fija los pines en construcción.

## Power

- Panel y placa **alimentados por separado**:
  - Panel: 5V externa al cable de entrada del panel HUB75 (con condensador de 1000µF entre + y − cerca del panel ayuda con los picos).
  - Matrix Portal: 5V por USB-C, o por su propio jack si lo tiene.
- Conectar masas: la masa del panel y la masa del Matrix Portal deben estar unidas (el conector HUB75 lleva GND).

## Partition layout sugerido (8MB)

```
# partitions_ota.csv
# Name,    Type, SubType,  Offset,   Size,
nvs,       data, nvs,      0x9000,   0x5000,    # 20KB
otadata,   data, ota,      0xe000,   0x2000,
app0,      app,  ota_0,    0x10000,  0x300000,  # 3MB slot OTA A
app1,      app,  ota_1,    0x310000, 0x300000,  # 3MB slot OTA B
littlefs,  data, spiffs,   0x610000, 0x170000,  # 1.4MB filesystem
coredump,  data, coredump, 0x780000, 0x10000,
```

En `platformio.ini`:
```ini
board_build.partitions = partitions_ota.csv
board_build.filesystem = littlefs
```

**Observaciones**:
- `app0/app1` de 3MB porque Arduino + WiFi + AsyncWebServer + ArduinoJson se va a ~1MB; con margen de sobra.
- `littlefs` con subtype `spiffs` (es el subtype legacy que la lib LittleFS espera).
- NVS de 20KB es lo estándar — pero ten en cuenta el gotcha #2 abajo.

## Gotcha #2: NVS para blobs grandes fragmenta y falla silente

NVS es un key-value store optimizado para **pares pequeños** (uint, strings cortos <24 bytes que caben inline). Cuando guardas blobs de varios KB con escrituras frecuentes, cada `putBytes` deja entries marcadas deleted y NVS GC sólo reclama páginas full-deleted.

Tras decenas de saves la partición de 20KB se satura: `prefs.putBytes` empieza a devolver 0 (write fail) silentemente. Las llamadas posteriores a `getBytes` devuelven el blob viejo (o nada, dependiendo del estado), y al siguiente reboot puedes encontrarte con que se han perdido claves enteras del namespace — incluidas otras claves no relacionadas.

**Regla**:
- NVS → solo para claves pequeñas (wifi creds, contadores, flags, valores de hardware como `rgb_order`).
- Blobs grandes (config JSON, cache de datos) → **LittleFS**, fichero `.json` con write+rename atómico.

## Gotcha #3: LittleFS partition label

`LittleFS.begin()` por defecto busca una partición de **nombre `"spiffs"`**, no de subtype. Si tu partición se llama `"littlefs"` (con subtype `spiffs`), falla al montar con `partition "spiffs" could not be found`.

Fix: pasar el label explícito:
```cpp
LittleFS.begin(/*formatOnFail=*/true, "/littlefs", 10, "littlefs");
//                                                       ^^^^^^^^^^
//                                                       partition label
```

Si la primera vez falla por falta de formato, `formatOnFail=true` la formatea automáticamente.

## Gotcha #4: USB CDC reset trick falla en S3

PIO usa la línea `DTR/RTS` del puerto serie para forzar la entrada al bootloader del ESP32 (`esptool` reset trick). En el S3 con Arduino corriendo y USB CDC activo, esto **no siempre funciona** — el firmware Arduino "secuestra" el USB y a veces no detecta el handshake del bootloader.

**Workaround**: BOOT+RESET físico:
1. Mantener pulsado el botón `BOOT`.
2. Pulsar y soltar `RESET`.
3. Soltar `BOOT`.

La placa entra en ROM bootloader y `pio run -t upload` funciona fiable. Tras subir, `RESET` corto vuelve a arrancar el firmware.

Este truco también recupera placas que han quedado en estado raro tras un OTA fallido a media subida.

## OTA — métodos y fiabilidad

### ArduinoOTA (espota.py, puerto 3232)

```cpp
ArduinoOTA.setHostname("device");
ArduinoOTA.setPassword("xxx");
ArduinoOTA.begin();
// loop():
ArduinoOTA.handle();
```

PIO env:
```ini
[env:ota]
upload_protocol = espota
upload_port = 192.168.X.Y
upload_flags =
    --auth=xxx
```

**Pros**: rápido cuando funciona (~30-60s para 1MB).
**Cons**:
- Requiere conectividad bidireccional: el device abre TCP de vuelta hacia la IP del Mac → **macOS firewall lo bloquea por defecto**. Hay que permitir Python en System Settings → Network → Firewall, o desactivarlo durante el upload.
- Sensible a wifi marginal: con RSSI < -75 dBm las retransmisiones TCP hacen que se cuelgue al final.

### Web upload (HTTP multipart)

Endpoint custom en el firmware:
```cpp
server.on("/api/firmware", HTTP_POST,
    [](AsyncWebServerRequest* req) {
        bool ok = !Update.hasError();
        req->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "...");
        if (ok) g_pendingReset = true;
    },
    [](AsyncWebServerRequest* req, const String& filename, size_t index,
       uint8_t* data, size_t len, bool final) {
        if (index == 0) {
            if (Update.isRunning()) Update.abort();   // recovery de OTA fallido previo
            Update.begin(UPDATE_SIZE_UNKNOWN);
        }
        if (len) Update.write(data, len);
        if (final) Update.end(true);
    });
```

Uso:
```bash
curl -F "firmware=@.pio/build/ENV/firmware.bin" http://192.168.X.Y/api/firmware
```

**Pros**:
- Más fiable con wifi marginal (transferencia HTTP unidireccional, gestión de conexión más robusta que ArduinoOTA UDP/TCP).
- No depende del firewall del Mac.
- Funciona desde cualquier cliente HTTP, no requiere `espota.py`.

**Cons**:
- Hay que implementar el endpoint y la UI/curl client.

**Recomendación**: implementar AMBOS. ArduinoOTA para iteración rápida desde PIO; web upload como fallback fiable.

### HTTPS APIs externos

Para llamar APIs HTTPS desde el ESP32:
```cpp
#include <WiFiClientSecure.h>
WiFiClientSecure client;
client.setInsecure();   // sin pinning de certs root; aceptable en LAN/internal
HTTPClient http;
http.begin(client, "https://api.example.com/...");
```

Embedir certs root es posible pero pesa flash y rota cada cierto tiempo — para uso doméstico `setInsecure()` es razonable.

## Boot recovery cuando todo va mal

Si la placa entra en bootloop por código corrupto, OTA fallido, NVS corrupto, etc:

1. **BOOT+RESET físico** (ver gotcha #4) — entra ROM bootloader incondicionalmente.
2. `pio run -t upload` con un firmware sano por USB.
3. Si NVS está corrupta y quieres factory-reset: borrar la partición NVS:
   ```bash
   esptool.py --port /dev/cu.usbmodem101 erase_region 0x9000 0x5000
   ```
   Tras eso el firmware seedeará defaults al próximo boot.
4. Para borrar TODO el flash (último recurso, pierde tu app y datos):
   ```bash
   esptool.py --port /dev/cu.usbmodem101 erase_flash
   ```

## Build setup (PlatformIO)

```ini
[env:matrixportal_s3]
platform = espressif32
board = adafruit_matrixportal_esp32s3
framework = arduino
monitor_speed = 115200
upload_speed = 921600
upload_port = /dev/cu.usbmodem101
monitor_port = /dev/cu.usbmodem101
board_build.partitions = partitions_ota.csv
board_build.filesystem = littlefs
build_flags =
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
lib_deps =
    mrcodetastic/ESP32 HUB75 LED MATRIX PANEL DMA Display
    bblanchon/ArduinoJson@^7
    me-no-dev/ESPAsyncWebServer
    me-no-dev/AsyncTCP
```

PSRAM se inicializa solo via `BOARD_HAS_PSRAM`. Para usarla explícitamente: `ps_malloc()` o variantes.

## Performance / capabilities

- **Refresh panel**: 60-120Hz fácil con DMA y doble buffer.
- **Color depth**: 8-bit por canal (24-bit total) configurable; 12-bit es el sweet spot de calidad/memoria.
- **Brillo**: `dma->setBrightness8(uint8_t)` — control hardware vía PWM, sin coste de CPU.
- **PSRAM disponible**: ~2MB después de boot. Útil para framebuffers HUB75 (que pueden ser 32KB-64KB) y para buffers de red grandes.
- **Heap libre típico**: ~180KB tras setup() con WiFi + AsyncWebServer arrancados.

## Útiles fragmentos

### Detectar PSRAM al boot
```cpp
if (psramInit()) Serial.printf("[psram] %u bytes free\n", ESP.getFreePsram());
```

### Boot reason
```cpp
esp_reset_reason_t r = esp_reset_reason();
// ESP_RST_POWERON, ESP_RST_SW (esp_restart), ESP_RST_PANIC, ESP_RST_BROWNOUT, etc.
```

### Stats NVS para diagnóstico
```cpp
#include <nvs.h>
nvs_stats_t s = {};
nvs_get_stats(NULL, &s);
Serial.printf("NVS used=%u free=%u total=%u namespaces=%u\n",
    s.used_entries, s.free_entries, s.total_entries, s.namespace_count);
```

### Hostname mDNS
```cpp
#include <ESPmDNS.h>
MDNS.begin("device-name");   // accesible como device-name.local
MDNS.addService("http", "tcp", 80);
```

## Símbolos de Arduino que colisionan

- `class Server` (en Arduino Ethernet/WiFi) → no llames a tu módulo `Server`. Renombra a `WebApi`, `HttpApi`, etc.
- Macros `min`/`max` → ArduinoJson y otros se llevan mal con ellas. En código moderno usar `std::min`/`std::max` o paréntesis defensivos.

## Checklist nuevo proyecto sobre Matrix Portal S3

- [ ] `partitions_ota.csv` con app0/app1 de 3MB y littlefs ≥1MB
- [ ] `LittleFS.begin(true, "/littlefs", 10, "littlefs")` con label explícito
- [ ] NVS sólo para claves pequeñas; config grande → fichero JSON en LittleFS
- [ ] `rgb_order` configurable en runtime (no hardcoded)
- [ ] OTA: ArduinoOTA + endpoint web `/api/firmware` (multipart)
- [ ] AP fallback si no hay creds wifi (SSID setup-XXX, IP 192.168.4.1)
- [ ] mDNS `<name>.local`
- [ ] Botón físico `BOOT+RESET` documentado para recovery
- [ ] Power: panel con 5V externa, NO desde USB-C del Matrix Portal
