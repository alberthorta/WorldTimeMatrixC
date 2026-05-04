# WorldTime — Firmware C++ (Arduino / PlatformIO)

Reescritura en C++ del firmware del reloj mundial WorldTime para Adafruit Matrix Portal S3, sustituyendo la versión CircuitPython (`firmware/`) por una basada en Arduino + ESP-IDF.

## Por qué este rewrite

Equivalente funcional (cities, brillo, modo noche, blink, paleta editable, iconos animados con desync, $DATE, backup/restore, WiFi setup AP fallback) más:

- **Animaciones fluidas durante operaciones HTTP**: panel refrescado por DMA en hardware, web server en task FreeRTOS independiente. La versión CircuitPython tenía un solo hilo Python, así que cualquier petición web pausaba el avance de frames.
- **Brillo real por hardware**: `setBrightness8` controla el OE del HUB75 vía PWM. Sin reescalar paletas. Inmediato y sin coste CPU.
- **Color depth 12-bit** (vs 4-bit en CP): degradados de temperatura mucho más finos.
- **Refresh rate ~120Hz** (vs ~60-100Hz en CP).

Trade-offs: pierde REPL y hot-reload de fichero único. Cada cambio requiere build + flash (~20s USB).

## Hardware

- **Adafruit Matrix Portal S3** (ESP32-S3, 8MB flash, 2MB PSRAM).
- **Panel HUB75 P5 64x32**, 1/16 scan, driver Epstar/Silan.
- **Fuente externa 5V** al cable de alimentación del panel (no por el USB del Matrix Portal).

Pines GPIO (en `src/Display.cpp`): R1=42 G1=40 B1=41 R2=38 G2=37 B2=39 (G/B intercambiados respecto al orden estándar — este panel concreto cablea así). A=45 B=36 C=48 D=35 E=-1 (no usado en 1/16). LAT=47 OE=14 CLK=2.

## Estructura del proyecto

```
firmware-cpp/
├── platformio.ini          # build config (dos envs: usb y ota)
├── partitions_ota.csv      # 2 slots OTA de 3MB + LittleFS 1.4MB
├── data/
│   └── index.html          # placeholder; el HTML real está embebido
├── src/
│   ├── main.cpp            # setup() + loop con render @10fps
│   ├── Config.h/.cpp       # NVS-backed (cities, brillo, paleta, iconos)
│   ├── WifiSetup.h/.cpp    # STA con fallback AP "WorldTime-Setup"
│   ├── Display.h/.cpp      # HUB75-DMA + tom-thumb + animation state
│   ├── Icons.h/.cpp        # 9 iconos x N frames, paleta 16 colores
│   ├── Weather.h/.cpp      # Open-Meteo client + cache NVS + task
│   ├── WebApi.h/.cpp       # AsyncWebServer + endpoints
│   ├── IndexHtml.h/.cpp    # UI completa embebida en flash
│   └── (no Server.h: nombre colisiona con clase del Arduino core)
└── README.md
```

## Setup desde cero

### Requisitos

```bash
brew install platformio       # macOS
# o pipx install platformio
```

### Primer flash via USB

1. Conecta la Matrix Portal S3 al Mac. macOS la verá como `/dev/cu.usbmodem101` (o similar).
2. Si el puerto difiere, edita `upload_port` en `platformio.ini`.
3. Compila + flashea:

   ```bash
   cd firmware-cpp
   pio run -e matrixportal_s3 -t upload
   ```

   Tarda ~20s. Tras el flash, la placa reinicia. Sin creds WiFi en NVS, levanta el AP `WorldTime-Setup` (pwd `matrixportal`).

4. **Configura WiFi**:
   - Conecta tu Mac/móvil al WiFi `WorldTime-Setup` con password `matrixportal`.
   - Abre `http://192.168.4.1/` en el navegador.
   - Busca redes → selecciona la tuya → password → "Conectar y reiniciar".
   - La placa se reinicia, conecta a tu WiFi, obtiene IP por DHCP.

5. **Encuentra la IP nueva**:
   ```bash
   for ip in 192.168.X.{50..120}; do
     curl -sS -m 1 "http://$ip/api/status" 2>/dev/null | grep -q wifi_mode && echo "$ip"
   done
   ```
   Sustituye `192.168.X.` por el prefijo de tu LAN.

6. Abre `http://<ip>/` y ya tienes la admin UI completa.

## Admin web UI

Navega a `http://<ip>/`. Secciones:

- **Estado**: IP, uptime, heap libre, RSSI.
- **WiFi**: scan + cambio de red (con reset automático).
- **Brillo (día)**: slider con preview live (sin escribir NVS hasta Save).
- **Modo noche**: toggle + ventana horaria (start/end mins desde medianoche) + brillo nocturno.
- **Ciudades**: 4 ciudades fijas con color picker, nombre, lat, lon. Si el nombre es `$DATE`, esa fila muestra la fecha como `DD/YY` en lugar del literal (la hora y meteo siguen funcionando con su lat/lon).
- **Iconos**: editor con paleta de 16 colores, frames múltiples, ms por frame, botón ▶ Play para preview en navegador. Iconos con >1 frame se animan en el panel; filas con el mismo icono arrancan con phase desync (`row_idx % len(frames)`).
- **Otros**: parpadeo del `:` cada segundo, intervalo de refresh de meteo (>= 30s).
- **Logs meteo**: tabla con offset/temp/code/day por ciudad.
- **Backup / Restaurar**: descarga JSON completo (sin creds WiFi) o carga uno previo.
- **Guardar / Recargar / Reiniciar device**.

## API HTTP

| Endpoint | Método | Cuerpo | Descripción |
|---|---|---|---|
| `/` | GET | — | UI HTML embebida |
| `/api/status` | GET | — | IP, uptime, heap, psram, RSSI, modo WiFi |
| `/api/wifi` | GET | — | Modo (sta/ap/none), SSID actual, IP |
| `/api/wifi` | POST | `{ssid, password}` | Guarda creds en NVS y reinicia |
| `/api/wifi/scan` | GET | — | Lista de redes cercanas (ssid, rssi, secure) |
| `/api/config` | GET | — | Config completa JSON (sin wifi) |
| `/api/config` | POST | parche JSON | Aplica + persiste; si cambian cities, refetch meteo |
| `/api/brightness` | POST | `{brightness?, night_brightness?}` | Live, sin escribir NVS |
| `/api/weather` | GET | — | Estado meteo runtime + diagnostics |
| `/api/weather/fetch?idx=N` | GET | — | Forzar fetch sincrono de una ciudad (debug) |
| `/api/firmware` | POST | binario .bin | OTA via HTTP (ver caveat abajo) |
| `/api/fs/index.html` | PUT | HTML | Hot-reload de UI (ver caveat abajo) |
| `/api/reset` | POST | — | Reinicia el device |
| `/debug/fs` | GET | — | Estado de LittleFS |

## Modelo de configuración (NVS)

Toda la config persiste en NVS de ESP32, namespace `worldtime`:

| Clave | Tipo | Descripción |
|---|---|---|
| `wifi_ssid` | string | SSID WiFi (separado de cfg para no aparecer en backups) |
| `wifi_pwd` | string | Password WiFi |
| `cfg` | blob | JSON serializado con cities + brillo + iconos + paleta + ... |
| `wxcache` | blob | Cache de última meteo conocida (restaurada al boot) |

**¿Por qué blob (`putBytes`) y no string?** Los strings NVS tienen un techo de ~4000 bytes. Con muchos frames de iconos el JSON puede superarlo, y `putString` falla silenciosamente — peor, podría afectar otras claves del namespace (incluida la WiFi). Usar blob evita ese límite.

Estructura del JSON `cfg`:

```json
{
  "brightness": 0.5,
  "weather_refresh_sec": 60,
  "colon_blink": true,
  "cities": [{"name":"BCN","lat":...,"lon":...,"color":13421772}, ...],
  "night_mode": {"enabled":false,"start_mins":1320,"end_mins":420,"brightness":0.1},
  "palette": [0, 16768000, ...],
  "icons": {"SUN":[{"px":[[...]],"ms":500}], ...}
}
```

## Animación de iconos

Cada icono es una lista de frames; cada frame es 5×5 píxeles (índices a paleta de 16) más una duración en ms. Los 9 iconos por defecto (`SUN`, `PARTLY`, `CLOUD`, `RAIN`, `SNOW`, `STORM`, `FOG`, `MOON`, `PARTLY_NIGHT`) arrancan con un solo frame; el usuario puede añadir más desde el editor.

State per fila (en `Display.cpp`):
- `iconType`: enum del icono asignado actualmente.
- `frameIdx`: frame visible.
- `nextChangeMs`: cuándo avanzar.

Cuando cambia el icono asignado a una fila, se reinicia con `frameIdx = rowIdx % numFrames`. Eso desincroniza dos filas que muestran el mismo icono.

`tickRowAnim()` se llama desde `renderRows()` cada 100ms (10fps). Compara `millis()` con `nextChangeMs`, avanza si toca, repinta el bitmap interno. El panel sigue refrescándose por DMA al margen de eso.

## Brillo y modo noche

`Display::setBrightness(uint8_t)` llama a `dma_display->setBrightness8()` que ataca el OE del panel vía PWM en hardware. Inmediato, sin coste CPU, sin reescalar paletas.

El loop, cada render, calcula `effectiveBrightness(utc, refOffset)`:
- Si modo noche está activo y la hora local de la primera ciudad cae en la ventana, usa `night_mode.brightness`.
- Si no, usa `brightness`.

El cambio se aplica solo si difiere del último valor (evita escribir al hardware sin necesidad).

## Despliegue / actualización

Tres caminos para subir nuevas versiones del firmware:

### 1. USB (recomendado en desarrollo)

```bash
pio run -e matrixportal_s3 -t upload
```

Rápido y fiable. ~20s.

### 2. ArduinoOTA (sin USB, requiere setup de firewall)

El env `ota` en `platformio.ini` está configurado:

```bash
pio run -e ota -t upload
```

Sub-bug conocido: en macOS el firewall bloquea el puerto entrante temporal que abre `espota.py` para que la placa empuje el binario. Hay que permitir Python en el firewall (Preferencias → Red → Firewall → Opciones) o ejecutar con firewall desactivado.

### 3. HTTP push (curl, scriptable)

```bash
curl -X POST --data-binary @.pio/build/matrixportal_s3/firmware.bin \
     -H "Content-Type: application/octet-stream" \
     http://<ip>/api/firmware
```

Caveat: con binarios > ~140KB, AsyncTCP no aguanta el backpressure de las escrituras a flash y la conexión se rompe. Útil solo para parches pequeños o experimentación. ArduinoOTA es más robusto.

## Backup / restore de config

```bash
# Descargar config actual (JSON, sin creds WiFi)
curl http://<ip>/api/config > backup.json

# Restaurar
curl -X POST -H "Content-Type: application/json" \
     --data-binary @backup.json http://<ip>/api/config
```

Las creds WiFi nunca aparecen en `/api/config`. Para cambiarlas hay que ir por `/api/wifi`.

## Limitaciones conocidas

1. **LittleFS no monta** en esta placa (motivo aún sin diagnosticar). Por eso el HTML está embebido en flash (`IndexHtml.cpp`) en lugar de servido desde FS. Implica que cualquier cambio en la UI necesita rebuild + flash, no se puede hot-reload con `PUT /api/fs/index.html` (el endpoint existe pero no funciona sin LittleFS).
2. **HTTP OTA con binarios grandes**: backpressure en AsyncTCP. Usar ArduinoOTA o USB.
3. **USB CDC serial**: `pio device monitor` no captura output fiablemente al boot. Debug por endpoints HTTP (`/api/status`, `/api/weather`, `/debug/fs`).
4. **OTA de PlatformIO en macOS**: requiere allow de firewall.
5. **Iconos compartidos**: las 9 categorías están hardcoded por nombre. Para añadir/eliminar tipos de icono hay que tocar `Icons.cpp` + `Display::IconType` + `Weather::iconForCode`.
6. **Cities fijas (4)**: la versión Python soportaba lista variable; aquí son siempre 4 (matchea las 4 filas del panel).

## Recuperación de emergencia

Si la placa se queda colgada o sin WiFi:

- **Reset por USB**: pulsa el botón RESET físico.
- **Recovery via AP**: si las creds WiFi están corruptas o no conecta, al boot levanta el AP `WorldTime-Setup`. Conéctate y reconfigura.
- **Wipe completo**: `pio run -t erase` borra TODO incluido NVS — perderás creds WiFi y config. Tras eso, USB upload y reconfigurar desde AP.

## Fases del desarrollo (lo hecho)

| Fase | Descripción | Estado |
|---|---|---|
| 1 | WiFi STA + AP fallback + UI mínima | ✓ |
| 2 | Render estático panel HUB75 + tom-thumb | ✓ |
| 3 | NTP + Open-Meteo fetch + task FreeRTOS | ✓ |
| 4 | Config NVS + admin UI editable | ✓ |
| 5 | Motor de animación de iconos (multi-frame, phase desync) | ✓ |
| 6 | UI completa con editor de iconos + paleta + backup | ✓ |
| 7 | OTA + LittleFS hot-reload | ✓ ArduinoOTA + HTTP / ✗ LittleFS |
| 8 | Botones físicos + pulido | pendiente |

## Comparativa rápida con `firmware/` (CircuitPython)

| | CircuitPython (`firmware/`) | Arduino C++ (`firmware-cpp/`) |
|---|---|---|
| Hot-reload | Edita 1 fichero, Ctrl-D, listo | Build + flash (~20s) |
| Animaciones bajo carga HTTP | Pausa visible | Smooth (DMA + tasks) |
| Brillo | Reescalado de paletas (CPU) | `setBrightness8()` (hardware PWM) |
| Color depth | 4-bit | 12-bit |
| REPL | `/cp/serial/` WS | No (Serial USB CDC, flaky) |
| Edición remota de config | `/api/config` POST | `/api/config` POST |
| Edición remota del código | `PUT /fs/code.py` | OTA del firmware completo |

Para uso normal (editar config desde la admin web), las dos versiones son equivalentes. Para iteración del propio firmware, CircuitPython gana en velocidad de feedback; C++ gana en performance final.
