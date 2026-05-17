# CLAUDE.md — WorldTimeMatrixC

Guía operacional para continuar el firmware C++ del WorldTime Matrix sobre Adafruit Matrix Portal S3.

## Qué es

Reescritura en Arduino/PlatformIO del proyecto Python original (vive en `/Users/albert/projects/WorldTimeMatrixPortal/`, CircuitPython). Misma funcionalidad: 4 ciudades, hora local, meteo Open-Meteo, iconos animados, `$DATE`, brillo configurable, modo noche, paleta + iconos editables, AP fallback para WiFi setup.

Por qué C++ vs CP: animaciones smooth durante operaciones HTTP (DMA + tasks FreeRTOS independientes), brillo real por hardware (`setBrightness8`), color depth 12-bit, refresh ~120Hz.

Repo: `git@github.com:alberthorta/WorldTimeMatrixC.git` (privado).

## Hardware

- **Adafruit Matrix Portal S3** (ESP32-S3, 8MB flash, 2MB PSRAM).
- **Panel HUB75 P5 64×32**, 1/16 scan, driver Epstar/Silan.
- **Fuente 5V externa** al cable del panel (NO por el USB-C del Matrix Portal).

GPIO base (`MTX_*` de CircuitPython): R1=42 G1=41 B1=40 R2=38 G2=39 B2=37 A=45 B=36 C=48 D=35 E=-1 LAT=47 OE=14 CLK=2.

### Botones tactiles externos (TTP223)

Tres sensores capacitivos TTP223 cableados a los pads analogicos para control sin abrir la caja:

| Pad | GPIO | Funcion |
|---|---|---|
| A2 | 9  | Izquierda → brillo -5% |
| A3 | 10 | Centro    → cicla modo display (FOUR_ROWS → FOCUS → CLAUDE) |
| A4 | 11 | Derecha   → brillo +5% |

- **NO usar A1 (GPIO 3)**: es strapping pin del ESP32-S3 (JTAG signal source). Conectar ahi un TTP que arranca en estado indefinido bloquea el boot del panel.
- `pinMode(pin, INPUT)` sin pull interno — el TTP223 tiene salida push-pull activa-HIGH.
- Sin debounce HW: el chip ya filtra contacto humano. Pero el switching del HUB75 genera ruido EMI que puede meter pulsos espurios. Filtro SW en `main.cpp`: una transicion solo se acepta tras `STABLE_THRESH` (3) lecturas consecutivas iguales — ~15ms de latencia, transparente.

### Anti-ruido del HUB75 sobre los TTP223

El switching del panel (CLK 25 MHz, OE rapido, lineas address conmutando) acopla EMI a los cables de output de los TTP223. Si el filtro SW de estabilidad no es suficiente (sigues viendo pulsaciones fantasma), ataja el problema en HW por orden de eficacia:

1. **Acortar el cable del OUT**. Cualquier cable largo o suelto entre el pin OUT del TTP y el pad del MatrixPortal actua de antena. < 5 cm idealmente, trenzado con el GND si va paralelo.
2. **Condensador 100 nF ceramico** entre OUT y GND, pegado al pin del ESP (no al TTP). Filtra el ruido de alta frecuencia que recoge el cable hacia GND antes de que llegue al GPIO.
3. **Condensador de desacoplo en la alimentacion del TTP**: 10–100 µF electrolitico entre VCC y GND de cada TTP, pegado al modulo. El rail 3V3 del MatrixPortal puede tener ripple por la carga conmutada del panel; estabilizarlo evita que el TTP oscile.
4. **Ferrita o resistencia serie 1 kΩ** en el cable OUT, cerca del TTP. Limita el slew rate del flanco y reduce reflexiones en cable largo.
5. **Comprobar GND comun**: VCC del TTP a 3V3 del MatrixPortal Y GND del TTP a GND del MatrixPortal con cables independientes (no compartir GND con el panel HUB75, cuya corriente de retorno mete ruido en el plano de tierra).

Si todo lo anterior falla, subir `STABLE_THRESH` en `main.cpp` a 4-5 (a costa de mas latencia).

## Devices conocidos en mi LAN

| IP | rgb_order | Notas |
|---|---|---|
| 192.168.53.62 | RBG | Panel con G/B swapped. Funcional. |
| 192.168.53.51 | RGB | Panel estándar. Tras un OTA fallido a 98% quedó respondiendo, requiere flash USB con BOOT+RESET manual para volver a actualizar fiablemente. |

AP fallback: SSID `WorldTime-Setup`, password `matrixportal`, IP `192.168.4.1`.

mDNS: cada device se anuncia como `WorldTimeXXX.local` donde XXX es el último octeto de su IP (p.ej. `WorldTime62.local`).

## Build & deploy

```bash
cd /Users/albert/projects/WorldTimeMatrixC

# USB (rápido, fiable, ~20s):
pio run -e matrixportal_s3 -t upload
# Si la placa no entra en bootloader auto: BOOT+RESET físico, luego upload.

# OTA via espota (1-3 min, lento por WiFi mediocre + flash):
pio run -e ota -t upload
# El target IP está en platformio.ini (upload_port). Cambiar a la IP del device.

# OTA via web UI (más estable):
# Abre http://<ip>/, sección "Actualizar firmware (OTA)", file picker + botón.
```

## Estructura

```
src/
├── main.cpp             # setup + loop @100ms + ArduinoOTA.handle + g_pendingReset
├── Config.h/cpp         # NVS-backed (cities, brillo, palette, iconos, rgb_order, etc.)
├── WifiSetup.h/cpp      # STA con fallback AP + mDNS WorldTimeXXX.local
├── Display.h/cpp        # HUB75-DMA + tom-thumb + per-row icon animation state
├── Icons.h/cpp          # 9 iconos × N frames, paleta 16 colores
├── Weather.h/cpp        # Open-Meteo fetch en task FreeRTOS + cache NVS
├── WebApi.h/cpp         # AsyncWebServer + endpoints
├── IndexHtml.h/cpp      # UI completa embebida en flash
└── (no Server.h: el nombre colisiona con clase del Arduino core)
data/index.html          # placeholder; el HTML real está embebido en IndexHtml.cpp
partitions_ota.csv       # 2 slots OTA de 3MB + LittleFS 1.4MB (no usado, ver gotcha 2)
platformio.ini           # dos envs: matrixportal_s3 (USB) y ota (espota)
```

## Arquitectura runtime

- **Display**: el panel se refresca por DMA en hardware, independiente del CPU. Doble buffer (anti-flicker).
- **Weather task** en core 1 (FreeRTOS). Cada 60s fetch las 4 ciudades, escribe `data[]`, cachea en NVS. `requestRefresh()` la despierta antes (vía `xTaskNotifyGive`).
- **WebServer** (AsyncWebServer) en su task interna. POST grandes acumulan en `_tempObject` antes de procesar.
- **Loop principal**: render @100ms (10fps). Lee `Config::cfg` + `Weather::data` + hora local con offset, monta `Display::Row[4]`, llama `renderRows`. Aplica brillo efectivo con modo noche.

## API HTTP

| Método | Path | Notas |
|---|---|---|
| GET | `/` | UI HTML embebida |
| GET | `/api/status` | IP, uptime, heap, psram, RSSI, modo WiFi |
| GET | `/api/wifi` | Modo, SSID actual, hostname, IP |
| GET | `/api/wifi/scan` | Lista de redes cercanas |
| POST | `/api/wifi` | `{ssid, password}` → guarda en NVS y reinicia |
| GET | `/api/config` | Config completa JSON (sin wifi creds) |
| POST | `/api/config` | Patch JSON; `cities_changed=true` → `Weather::requestRefresh()` |
| POST | `/api/brightness` | Live, sin escribir NVS |
| GET | `/api/weather` | Estado runtime + diagnostics |
| GET | `/api/weather/fetch?idx=N` | Forzar fetch sincrono (debug) |
| POST | `/api/firmware` | OTA via multipart upload |
| POST | `/api/reset` | Reinicia (`g_pendingReset = true`) |
| GET | `/debug/fs` | Estado de LittleFS (lista de ficheros) |
| GET | `/api/diag/nvs` | Snapshot de NVS + FS: bootCount, sizes, lastSave, etc. (debug del bug histórico de OTA-reseta-config) |

## Persistencia: NVS + LittleFS

**NVS (namespace `worldtime`)** — sólo strings cortos, robustos:
- `wifi_ssid`, `wifi_pwd` — creds. NUNCA aparecen en `/api/config` ni en backups.
- `boot_n` — contador de boots (instrumentación de diagnóstico, ver `Config::DiagInfo`).

**LittleFS (partition `littlefs`, 1.4MB)** — JSON grandes y escrituras frecuentes:
- `/cfg.json` — toda la config: cities, brightness, palette, icons, rgb_order, night_mode.
- `/wxcache.json` — última meteo conocida (re-escrito cada ~60s tras fetch).
- `/index.html` — UI servida por la web. Sembrada al boot desde `IndexHtml.cpp` si falta.

**Por qué FS y no NVS para los blobs**: NVS fragmenta con saves frecuentes de blobs grandes (cada `putBytes` deja entries marcadas deleted, GC sólo reclama páginas full-deleted). Tras decenas de saves la partición se satura, `putBytes` falla silente y los reboots cargan defaults — bug histórico que se manifestaba como "OTA reseta config" (ver memoria de proyecto). LittleFS aguanta KBs y miles de re-escrituras sin degradación.

`Config::begin()` migra automáticamente desde NVS legacy (string o blob `cfg`) a `/cfg.json` la primera vez que ve datos viejos; luego limpia las claves NVS. Idempotente.

Estructura del JSON en `/cfg.json`:

```json
{
  "brightness": 0.5,
  "weather_refresh_sec": 60,
  "colon_blink": true,
  "cities": [{"name":"BCN","lat":...,"lon":...,"color":13421772}, ...],
  "night_mode": {"enabled":false,"start_mins":1320,"end_mins":420,"brightness":0.1},
  "palette": [0, 16768000, ...],
  "icons": {"SUN":[{"px":[[...]],"ms":500}], ...},
  "rgb_order": "RGB"
}
```

## Gotchas conocidos

1. **G/B swap por panel**. Cada panel HUB75 cablea distinto los canales RGB. `rgb_order` en config = `"RGB"` (estándar) o `"RBG"` (G/B intercambiados). Cambio requiere reboot porque la lib HUB75-DMA fija pines en construcción.

2. **LittleFS partition label**. La partición se llama `"littlefs"` (subtype `spiffs`), pero `LittleFS.begin()` por defecto busca una partición de nombre `"spiffs"` y falla con `partition "spiffs" could not be found`. Fix: pasar el label explícito en cada `begin()`:
   ```cpp
   LittleFS.begin(true, "/littlefs", 10, "littlefs");
   ```
   Antes este gotcha hacía que el FS no montara y el HTML se servía desde flash embebido. Ahora monta y `cfg.json`/`wxcache.json` viven ahí. El HTML de `IndexHtml.cpp` sigue siendo el seed inicial al primer boot.

3. **OTA via espota lento**: 1-3 min con WiFi mediocre (RSSI < -70). Causas: flash erase ~10-50ms/sector × 256 sectores + retransmisiones TCP. Usar el web upload (`/api/firmware`, multipart) suele ir más estable.

4. **USB CDC reset trick**: PIO usa DTR/RTS para forzar bootloader. En S3 con firmware Arduino corriendo, no siempre funciona. Workaround: BOOT+RESET físico (mantener BOOT, pulsar RESET, soltar BOOT) y reintentar `pio run -t upload`.

5. **macOS firewall** bloquea el puerto entrante temporal de `espota.py`. Si OTA cuelga al final del upload, permitir Python en Firewall (System Settings → Network → Firewall).

6. **`name` collision**: `class Server` existe en Arduino core. Por eso el módulo se llama `WebApi`, no `Server`.

7. **Cities fijas a 4**. La versión Python soportaba lista variable (1-4); aquí siempre 4 para simplicidad. Para añadir/eliminar ciudades hay que cambiar el array fijo en `Config.cpp`.

8. **`$DATE` como nombre de ciudad**: en render, esa fila muestra `DD/MM` (día/mes) en lugar del literal. Hora y meteo siguen funcionando con su lat/lon. Útil para una "fila de fecha".

9. **`Weather::requestRefresh()`**: cuando cambian cities NO invalidamos `data[]`. Solo despertamos la task para refetch inmediato. Así los iconos/temps no parpadean vacíos durante la edición.

10. **AsyncTCP en core 1 mata la WiFi percibida**. Por defecto `CONFIG_ASYNC_TCP_RUNNING_CORE=-1` deja que el scheduler decida, y como `WebApi::begin()` se llama desde `setup()` (core 1 en Arduino-ESP32), la service task de AsyncTCP acaba en core 1, compitiendo con el render loop y `weatherTask`. Síntoma: latencia HTTP alta y sensación de "WiFi mala" aunque la asociación esté bien (ping OK, RSSI razonable). Fix en `platformio.ini` build_flags: `-DCONFIG_ASYNC_TCP_RUNNING_CORE=0` la pinea a core 0 junto a WiFi/lwIP, donde ya está su counterpart natural; combinar con `-DCONFIG_ASYNC_TCP_QUEUE_SIZE=64` para holgura. El bump del render de 10 a 20-30fps fue lo que destapó este problema.

## Fases hechas / pendientes

| Fase | Estado |
|---|---|
| 1. WiFi STA + AP fallback + UI mínima | ✓ |
| 2. Render estático panel HUB75 + tom-thumb | ✓ |
| 3. NTP + Open-Meteo + task FreeRTOS | ✓ |
| 4. Config NVS + admin UI editable | ✓ |
| 5. Animación iconos multi-frame con phase desync | ✓ |
| 6. UI completa (icon editor + paleta + backup) | ✓ |
| 7. OTA: ArduinoOTA ✓ / HTTP multipart ✓ / LittleFS hot-reload ✓ | ✓ |
| 8. Botones físicos UP/DOWN + weather logs enriquecidos | pendiente |

## Comparativa rápida con el original CircuitPython

| | Python (`../WorldTimeMatrixPortal/`) | C++ (este proyecto) |
|---|---|---|
| Hot-reload | Edita 1 fichero, Ctrl-D | Build + flash (~20s USB / 1-3min OTA) |
| Animaciones bajo carga HTTP | Pausa visible | Smooth (DMA + tasks) |
| Brillo | Reescalado de paletas (CPU) | `setBrightness8()` (PWM hardware) |
| Color depth | 4-bit | 12-bit |
| REPL | `/cp/serial/` WS | No (Serial USB CDC, flaky) |
| Edición remota config | `/api/config` POST | `/api/config` POST (mismo schema) |
| Edición remota código | `PUT /fs/code.py` | OTA del firmware completo |

Para uso normal (editar config desde admin), las dos versiones son equivalentes. Para iterar sobre el propio firmware, CP gana en velocidad de feedback; C++ gana en performance final.

## Convenciones

- Comentarios en español, código en inglés (variables, funciones, identificadores).
- Mensajes de commit en voz del usuario (no atribuir a Claude/AI).
- Antes de tocar el device en producción (.62), preguntar; en dev (.51 o USB) más libre.
- Verificar build (`pio run`) antes de flashear.
- Cuando algo cambie comportamiento visible, decir al usuario qué esperar para que pueda verificar.
