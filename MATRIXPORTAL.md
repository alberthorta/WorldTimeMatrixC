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

## Gotcha #5: WiFi marginal — auto-rate adaptation tarda minutos en converger

Síntoma observado: tras boot/OTA, el HTTP server técnicamente está activo en pocos segundos (el reloj y los iconos pintan, lo que demuestra que `setup()` terminó), pero las requests entrantes a `/api/status` o cualquier endpoint timeoutean durante ~5 minutos. Después se estabilizan y van perfectas hasta el próximo reset.

**Causa raíz**: el WiFi del ESP32-S3 arranca asociándose al AP a la rate más alta que negocie (54 Mbps en 11g, más en 11n). Con RSSI marginal (-75 dBm o peor) esa rate falla por loss masivo. El driver tiene un algoritmo de auto-rate adaptation que detecta el loss y baja gradualmente la rate (54→24→12→6→1 Mbps), pero requiere tráfico sostenido para muestrear y converger. Sin ese tráfico al boot, el algoritmo tarda **minutos** en estabilizarse, durante los cuales casi todo el TCP entrante se pierde (lwip retransmits con RTOs inflados, el AP buffereando paquetes mal interpretando power-save, etc).

**Fix que resuelve el problema**: forzar 802.11b (DSSS modulation, mucho más robusta a baja RSSI) tras la asociación al AP.

```cpp
#include <esp_wifi.h>

WiFi.mode(WIFI_STA);
WiFi.setHostname("device-name");   // ANTES de begin (importante, ver abajo)
WiFi.setSleep(false);              // siempre awake (asume USB/mains)
WiFi.setAutoReconnect(true);
WiFi.begin(ssid, password);
// ... esperar WL_CONNECTED ...
esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B);
```

Después de esto el link queda en máx. 11 Mbps (≈6 Mbps reales) pero **estable desde el primer segundo**. Para JSON pequeño, OTAs de ~1MB y carga típica de un device IoT es de sobra.

**Detalles importantes**:

- **`setHostname` ANTES de `WiFi.begin()`**: si lo cambias después, DHCP ya negoció con el nombre auto-generado del chip. Algunos routers tardan minutos en propagar el cambio, dejando el device alcanzable solo via IP entretanto.
- **`WiFi.setSleep(false)`**: por defecto el ESP32 entra en sleep entre beacons (cliente IoT con batería). En APs marginales eso causa que el AP piense que el cliente duerme y bufferee paquetes entrantes, retrasando el TCP ack. Apagar power-save lo arregla. Coste: ~70 mA extra (irrelevante con USB/mains).
- **`setAutoReconnect(true)`**: si el link cae después, el driver intenta reasociarse solo.
- **Trade-offs de 11b**: max ~11 Mbps. Algunos APs "11n-only" o de empresa pueden rechazar clientes 11b (en LAN doméstica todos lo aceptan). Cliente 11b consume más air time por bit que 11n, ralentizando ligeramente el canal para los demás.

**Datos del proyecto** (RSSI -77 dBm):
- Antes: 0-1/19 requests OK durante los primeros 60s, ~5 min hasta estabilizarse.
- Después: 57/57 OK desde +5s. Boot a "fully usable" en menos de 6s.

**Cómo verificar si te afecta**: tras un boot, lanzar polling de un endpoint `/api/status` cada 0.5s desde otra máquina y medir el % de éxito en los primeros 30s. Si <50% durante un periodo extendido, casi seguro es esto y la solución 11b lo arregla.

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

## Otros periféricos disponibles

Lo que viene **en la placa** además del HUB75:

- **LIS3DH accelerómetro 3-axis** (I2C 0x18 o 0x19): detección de orientación, tap, motion. Lib `Adafruit_LIS3DH`. Útil para "girar la matriz", apagar al detectar que se ha tumbado, etc.
- **2× STEMMA QT / Qwiic** (I2C 3.3V): conectores plug-and-play para sensores externos. BME280 (temp/humedad/presión), BH1750 (lux), VEML7700, RTC DS3231 si quisieras hora con backup, OLED auxiliar, etc.
- **NeoPixel status LED** (GPIO ~4 o 33, verificar): un LED RGB direccionable. Útil como indicador secundario sin tocar la matriz.
- **Botones físicos**: `UP` y `DOWN` (verificar GPIO en doc Adafruit), más `BOOT` (GPIO0) y `RESET`. Útiles para navegación/menus sin web.
- **Conector JST 2-pin LiPo** + circuito de carga: opera sin USB con batería; carga al conectar USB.
- **GPIO breakout**: algunos pines libres expuestos en pads laterales. Útiles para PIR, encoder rotatorio, IR, etc.
- **Native USB** (CDC + JTAG via USB-OTG del S3): permite serial sin chip USB-UART, y JTAG sin probe externo.

Lo que viene **del SoC ESP32-S3** (no chip externo, está en silicio):

- **WiFi 2.4GHz** (b/g/n).
- **Bluetooth LE 5.0** (no Bluetooth Classic). BLE GATT server, advertising, beacon, mesh.
- **RTC interno**:
  - El S3 tiene RTC con dominios power persistentes durante deep-sleep.
  - **Sin backup de batería en la placa** — al cortar power la hora se pierde. Resync via NTP al boot, o añade DS3231 por STEMMA QT si necesitas hora offline.
- **ADC**: 2× SAR ADC, 12-bit, ~20 canales repartidos por GPIOs.
- **DAC**: el ESP32-S3 NO tiene DAC dedicado (a diferencia del ESP32 original). Si necesitas audio analógico, usar I2S + amp externo.
- **I2S audio**: 2 canales, soporta PDM (micrófonos digitales tipo SPH0645) y output a amp digital tipo MAX98357.
- **Touch sensor**: hasta 14 GPIOs configurables como touch capacitive (capacitive Touch v2).
- **PWM hardware (LEDC)**: 8 canales, hasta 14-bit resolution.
- **Crypto hardware**: AES, SHA-1/256/384/512, RSA, HMAC, RNG. Acelera HTTPS y firmas.
- **ULP coprocessor** (RISC-V): puede correr código durante deep-sleep para muestrear sensores con consumo µA.
- **Sensor de temperatura interno**: ~±5°C de precisión, mide la die. No vale para temperatura ambiente — afectado por self-heating.
- **NO tiene Hall sensor** (el ESP32 original sí, S3 lo eliminó).

## Snippets útiles

### LIS3DH acelerómetro
```cpp
#include <Adafruit_LIS3DH.h>
#include <Wire.h>
Adafruit_LIS3DH lis;
void setup(){
    Wire.begin(/*SDA=*/3, /*SCL=*/4);   // verificar pines I2C de la placa
    if (!lis.begin(0x18)) Serial.println("no LIS3DH");
    lis.setRange(LIS3DH_RANGE_2_G);
}
void loop(){
    lis.read();
    Serial.printf("x=%d y=%d z=%d\n", lis.x, lis.y, lis.z);
    delay(100);
}
```

### Bluetooth LE simple advertising
```cpp
#include <NimBLEDevice.h>
NimBLEDevice::init("device-ble");
NimBLEServer* s = NimBLEDevice::createServer();
NimBLEService* svc = s->createService("180F");   // battery service uuid
auto* ch = svc->createCharacteristic("2A19", NIMBLE_PROPERTY::READ);
ch->setValue(85);
svc->start();
NimBLEDevice::getAdvertising()->start();
```
Lib: `h2zero/NimBLE-Arduino` (mucho menor que el BLE stack stock).

### Sensor temp interno del S3
```cpp
#include <driver/temp_sensor.h>
temp_sensor_config_t t = TSENS_CONFIG_DEFAULT();
temp_sensor_set_config(t);
temp_sensor_start();
float c;
temp_sensor_read_celsius(&c);
Serial.printf("die=%.1fC\n", c);
```

### Deep sleep con wakeup
```cpp
esp_sleep_enable_timer_wakeup(60 * 1000000ULL);   // 60s
// O wakeup por GPIO (boton):
// esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
esp_deep_sleep_start();
```

### NeoPixel
```cpp
#include <Adafruit_NeoPixel.h>
Adafruit_NeoPixel led(1, /*pin*/4, NEO_GRB + NEO_KHZ800);
led.begin();
led.setPixelColor(0, led.Color(0, 32, 0));   // verde tenue
led.show();
```

## Lo que NO tiene

- **No SD card slot** en el Matrix Portal S3 (la M4 sí lo tenía).
- **No DAC analógico** (el ESP32-S3 lo eliminó).
- **No Hall sensor** (idem).
- **No RTC con backup** (necesita NTP o RTC externo en STEMMA QT).
- **No Ethernet** (sólo WiFi/BLE).

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
- [ ] WiFi: `setHostname` ANTES de `begin`, `setSleep(false)`, y `esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B)` post-asociación si el RSSI es marginal (ver Gotcha #5)
