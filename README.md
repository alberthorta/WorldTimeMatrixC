# WorldTime — Firmware C++ (Arduino / PlatformIO)

Reescritura en C++ del firmware del reloj mundial WorldTime para Adafruit Matrix Portal S3, sustituyendo la versión CircuitPython (`firmware/`) por una basada en Arduino + ESP-IDF.

> **Estado actual**: features completas. 6 modos de display, sensores táctiles con menú en pantalla, integración con Claude Code stats, jitter de ratón BLE, auto-update via GitHub releases, IP estática, programaciones, mascot animado, etc. Ver [releases](https://github.com/alberthorta/WorldTimeMatrixC/releases) para la última versión publicada.

## Por qué este rewrite

Equivalente funcional a la versión CircuitPython más:

- **Animaciones fluidas durante operaciones HTTP**: panel refrescado por DMA en hardware, web server en task FreeRTOS independiente.
- **Brillo real por hardware**: `setBrightness8` controla el OE del HUB75 vía PWM. Inmediato y sin coste CPU.
- **Color depth 12-bit** (vs 4-bit en CP): degradados de temperatura mucho más finos.
- **Refresh rate ~120Hz**.
- **Auto-update via GitHub Releases**: los devices se actualizan solos al boot y periódicamente.

Trade-offs: pierde REPL y hot-reload de fichero único. Cada cambio requiere build + flash (~20s USB) o esperar al auto-update.

## Hardware

- **Adafruit Matrix Portal S3** (ESP32-S3, 8MB flash, 2MB PSRAM).
- **Panel HUB75 P5 64×32**, 1/16 scan, driver Epstar/Silan.
- **Fuente externa 5V** al cable de alimentación del panel (no por el USB del Matrix Portal).

Pines GPIO HUB75 (en `src/Display.cpp`): R1=42 G1=41 B1=40 R2=38 G2=39 B2=37 A=45 B=36 C=48 D=35 E=−1 LAT=47 OE=14 CLK=2. El orden RGB es configurable desde la web (`rgb_order`: `RGB` o `RBG`) — algunos paneles cablean G/B intercambiados.

### Sensores táctiles externos (TTP223)

Tres sensores capacitivos cableados a los pads analógicos para control sin abrir la caja:

| Pad | GPIO | Función por defecto |
|---|---|---|
| A2 | 9  | Izquierda → modo de display anterior / navegar el menú |
| A3 | 10 | Centro → abre el menú en pantalla / acepta |
| A4 | 11 | Derecha → modo de display siguiente / navegar el menú |

- **NO usar A1 (GPIO 3)**: es strapping pin del ESP32-S3 (JTAG signal source). Conectar ahí un TTP en estado indefinido bloquea el boot del panel.
- `pinMode(pin, INPUT)` sin pull interno — el TTP223 tiene salida push-pull activa-HIGH.
- Filtro SW por tiempo continuo: solo se acepta `PRESSED` si el pin está HIGH durante 80 ms continuos, con 350 ms de lock post-pulsación. Resistente al ruido EMI del HUB75.
- Cada sensor se puede desactivar individualmente desde la web (solo bloquea el sensor físico; los botones simulados de la web siguen funcionando).

### Anti-ruido del HUB75 sobre los TTP223

El switching del panel acopla EMI a los cables de output. Si el filtro SW no es suficiente, atajar en HW por orden de eficacia:

1. **Acortar el cable** del OUT (< 5 cm, trenzar con GND).
2. **Condensador 100 nF cerámico** entre OUT y GND, pegado al pin del ESP.
3. **Condensador 10–100 µF** entre VCC y GND del TTP, pegado al módulo.
4. **Ferrita o resistencia serie 1 kΩ** en el cable OUT cerca del TTP.
5. **GND independiente** del MatrixPortal (no compartir con el cable del panel).

## Modos de display

Los botones **izquierda / derecha** (TTP A2 / A4) ciclan entre modos: derecha avanza, izquierda retrocede. El botón **centro** ya no cambia de modo — abre el [menú en pantalla](#menú-en-pantalla). También `POST /api/button?b=left|right`.

Orden del ciclo: 4 filas → Focus → Claude → Life → Imagen → Demoscene → (vuelta a 4 filas). El modo Claude se salta automáticamente si no hay `claude_session_key` configurada.

El modo activo vive solo en RAM: tras un reboot se vuelve al modo 1 (o al que indique una [programación](#programaciones-cambio-de-modo)).

### Modo 1 — 4 filas (FOUR_ROWS, default)

Las 4 ciudades configuradas, cada una con su nombre, hora, temperatura e icono animado. Crossfade de 300 ms al cambiar HH:MM o temperatura. Indicador de tendencia opcional. Si una ciudad se llama `$DATE`, esa fila muestra la fecha en lugar del literal.

### Modo 2 — Focus

Solo la primera ciudad ocupando los 64×32:
- **HH:MM** grande con fuente `FreeSans12pt7b` (~18 px alto) centrada arriba.
- **Fecha + icono animado + temperatura** en una fila inferior.
- **Segundera smooth** de 2 px alto, ancho completo, sub-pixel.
- Colores configurables (`focus_hour_color` / `focus_date_color`).
- Colon blink siguiendo el flag `colon_blink`.

### Modo 3 — Claude stats

Solo aparece si la `claude_session_key` está configurada. Layout 2/3 + 1/3:

**Columna izquierda (Claude stats)**
- Línea `5h NN%` + countdown en minutos (`180m`) con símbolo de reloj.
- Pace bar 5 h: barra de color por estado + marker blanco de elapsed.
- Línea `7d NN%` + countdown en horas (`96h`) + símbolo.
- Pace bar 7 d.
- Pace label centrada del 5 h en color por estado: `Well under` / `Under` / `On pace` / `Over` / `Burning`.

**Columna derecha (info de cities[0])**
- Hora HH:MM con crossfade + colon blink.
- Fecha siempre DD/MM en este modo.
- Icono del tiempo animado + temperatura en la misma fila.
- **Clawd** (mascot 14×10 naranja de Anthropic) abajo con animaciones (ver sección [Clawd](#clawd---mascot)).

**Barra de segundos** de 2 px de alto recorriendo toda la pantalla en y=30..31, separador vertical que se corta para no interrumpirla.

Datos: `GET https://claude.ai/api/organizations/{orgId}/usage` con cookie `sessionKey`. El `orgId` se descubre automáticamente al primer fetch exitoso. Cache en `/claudecache.json` (LittleFS) para sobrevivir reboots. Refresh configurable (default 180 s).

#### Clawd - mascot

Pixel art 14×10 con cabeza, ojos rectangulares, bracitos y patitas:

- **Mirada**: cada 4–10 s mira a izquierda o derecha (-1 / +1 px) durante 0.8–2.3 s y vuelve al frente.
- **Guiño**: cada 6–15 s ciclos de 2 guiños seguidos (60 ms cierre + 120 ms cerrado + 60 ms apertura).
- **Feliz**: cada 8–20 s pone cara sonriente (^_^) durante 4–10 s. Patrón V invertida en la row inferior del ojo. Durante happy no parpadea ni mira a los lados.
- **Saludo**: mientras está feliz, los 2 px de la punta de la mano derecha alternan posición cada 350 ms (waving).

### Modo 4 — Game of Life

Autómata celular de Conway corriendo a pantalla completa, con la fila inferior común (fecha + icono + temperatura + segundera).

- Color de las células configurable (`life_color`), o modo **arcoíris** (`life_rainbow`): el hue avanza unos grados en cada step.
- Velocidad de simulación configurable (`life_step_ms`, 50–1000 ms).

### Modo 5 — Imagen

Muestra una imagen del usuario de 64×23 px sobre la fila inferior común.

- Se sube desde la web: `POST /api/userimg` con el binario ya convertido a RGB565 (2944 bytes exactos = 64×23×2). La UI hace la conversión en el navegador.
- Se guarda en `/userimg.bin` (LittleFS), sobrevive reboots.

### Modo 6 — Demoscene

Cuatro efectos clásicos a pantalla completa, seleccionables con `demoscene_effect`:

| Valor | Efecto |
|---|---|
| 0 | Llama (fuego por convección, paleta Doom) |
| 1 | Plasma |
| 2 | Moiré |
| 3 | Nyan Cat (sprite animado + arcoíris) |

Los tres primeros comparten paleta: `fire_use_default` usa la clásica naranja, o `fire_color` genera una paleta custom a partir de un color. Nyan Cat no usa paleta (sprite propio).

## Menú en pantalla

El botón **centro** abre un menú overlay que tapa el reloj. Navegación uniforme con los tres botones:

- **izq / der** → mueven la selección (o cambian el valor, si estás editando un campo).
- **centro** → entra en la opción / entra a editar el campo / acepta el valor.
- La última fila de cada submenú es **Back**, que vuelve al menú principal.
- **Auto-cierre a los 20 s** sin tocar ningún botón. Si te pilla editando, el valor en curso se persiste antes de cerrar.

Opciones del menú principal:

| Opción | Qué hace |
|---|---|
| Brightness | Barra de brillo con `Level NN%`. Ajusta el brillo **efectivo**: si estás dentro de la ventana de modo noche toca `night_brightness`, si no el brillo de día. |
| Jitter | ON / OFF del [jitter BLE](#jitter--ratón-ble-anti-inactividad) + estado del host (`Mac linked` / `no Mac`). |
| Session | Auto «hola» diario: activar, hora y minuto (ver [Auto «hola»](#auto-hola-y-keep-awake)). |
| Keep Awake | ON / OFF de la renovación automática de la ventana de 5 h. |
| Exit | Vuelve al reloj. |

Las filas tienen tres estados visuales: **navegada** (barra rellena cian, texto oscuro), **en edición** (recuadro + chevrons `<` `>`), e **inactiva** (texto atenuado). Los cambios se aplican en vivo para dar feedback inmediato (el brillo dimea el panel, el jitter arranca/para) y se persisten al aceptar.

## Jitter — ratón BLE anti-inactividad

El device se anuncia por Bluetooth como ratón HID (`WorldTime Jitter`). Con el jitter activo mueve el cursor unos pocos píxeles cada intervalo (dirección y distancia aleatorias, con recentrado suave) para que el Mac no entre en reposo.

- **Emparejar**: macOS → Ajustes → Bluetooth → `WorldTime Jitter`.
- **Configuración** (`jitter_enabled`, `jitter_interval_ms` 50–600000, `jitter_max_step` 1–20 px): desde la pestaña *Jitter* de la web, desde el menú en pantalla, o por BLE.
- **Estado persistente**: si queda activo y el Mac sigue emparejado, al reboot el cursor vuelve a moverse solo.
- `GET /api/jitter` expone lo runtime que no persiste: si hay un host BLE conectado.

Implementado con **NimBLE** (más ligero que Bluedroid, importante por la RAM que ya consumen HUB75-DMA + WiFi + AsyncWebServer). Portado de `../gizmo/tablet/main/jitter.c`.

### App de barra de menú para macOS (`mac/`)

App Swift mínima que controla el jitter por BLE desde la barra de menú, usando el mismo servicio de control `6B1D0001-7C9A-4F3E-9B2A-1F4D3C2B1A00` que expone el firmware:

```bash
cd mac && ./build.sh          # genera WorldTimeJitter.app
cd mac && ./build.sh --zip    # además, WorldTimeJitter.zip para la release
```

Soporta varios dispositivos compatibles a la vez (el WorldTime Matrix y cualquier otro que exponga el servicio, p.ej. el tablet `Gizmo Jitter`); el menú *Dispositivo* elige a cuál mandar los comandos y recuerda la elección. El protocolo de bytes debe coincidir con `src/Jitter.cpp`.

#### Arranque automático

Opción **"Abrir al iniciar sesión"** en el menú. Usa `SMAppService.mainApp` (sin helper bundle separado). Requiere **macOS 13+**: en macOS 12 la opción no aparece. Si macOS rechaza el registro suele ser porque la app no está en `/Applications`.

#### Auto-actualización

La app comprueba las releases de GitHub del propio repo y se actualiza sola:

- **Al arrancar** (3 s después, para no competir con el escaneo BLE) si *"Buscar actualizaciones al arrancar"* está activo — por defecto sí. En este chequeo solo avisa si hay algo nuevo.
- **Bajo demanda** con *"Buscar actualizaciones ahora"*, que siempre responde algo.
- Al aceptar: descarga el asset `.zip` de la release, lo descomprime con `ditto`, escribe un script helper que espera a que el proceso muera, sustituye el bundle y relanza la app.

**La app comparte tag con el firmware.** Las releases llevan los dos assets (`firmware.bin` para el ESP32 y `WorldTimeJitter.zip` para el Mac) bajo la misma versión. Es deliberado: el protocolo BLE tiene que cuadrar entre firmware y app, así que versionarlos juntos evita combinaciones incompatibles. `build.sh` inyecta la versión desde `git describe` en el `Info.plist` del bundle (no en el fuente, para no ensuciar el repo en cada build): `CFBundleShortVersionString` = tag limpio (lo que se compara), `CFBundleVersion` = describe completo.

> Tras actualizar, **macOS vuelve a pedir permiso de Bluetooth una vez**: la app va firmada ad-hoc y el permiso está atado al cdhash del bundle, que cambia al sustituirlo.

Portado de [ClaudeStats](https://github.com/alberthorta/ClaudeStats) (`Core/LaunchAtLogin.swift`, `Core/UpdateChecker.swift`, `Core/UpdateInstaller.swift`).

## Auto «hola» y keep-awake

Dos mecanismos independientes (pueden estar ambos activos) para abrir o mantener viva la ventana de 5 h de Claude. Ambos hacen lo mismo por debajo: crean una conversación en claude.ai, mandan un completion mínimo («hola») y la borran.

**Auto «hola» diario** (`claude_auto_hola_enabled`, `claude_auto_hola_hour`, `claude_auto_hola_minute`)
- Dispara una vez al día a la hora local indicada (timezone de `cities[0]`, mismo criterio que modo noche y schedule).
- `claude_auto_hola_last_date` guarda el día local ya disparado (`YYYYMMDD`), así un reboot no lo re-dispara.
- El día se marca como hecho **pase lo que pase**: un fallo transitorio no debe spammear claude.ai. Un intento al día.

**Keep-awake** (`claude_keep_awake_enabled`)
- Vigila el `resetsAt` de la ventana de 5 h que ya trae ClaudeStats; cuando ese instante pasa, manda otro «hola» y abre una ventana nueva.
- Tras un «hola» exitoso el `resetsAt` salta ~5 h al futuro, lo que evita re-disparos hasta la siguiente expiración.
- Si el envío falla, reintenta a los ~60 s.

Ambos requieren `claude_session_key` configurada. Estado y último resultado en `GET /api/claude/hola`; disparo manual con `POST /api/claude/hola` (o el botón "Enviar «hola» ahora" de la web).

## Web admin UI

Navega a `http://<ip>/`. La UI está organizada en **pestañas**: *Botones*, *Pantalla*, *Modos*, *Iconos*, *Meteo* y *Jitter*. Secciones:

- **Estado** — IP, uptime, heap libre, RSSI.
- **Botones (simulación)** — Botones izq/centro/der que disparan la misma acción que los TTPs físicos (incluye ripple visual). Toggles individuales para activar/desactivar cada TTP físico (los botones de arriba siguen funcionando).
- **WiFi** — Scan + cambio de red. Toggle `Usar DHCP` (default ON) o IP estática con IP / Gateway / Subnet / DNS1 / DNS2.
- **Brillo (día)** — Slider con preview live (sin escribir NVS hasta Save).
- **Modo noche** — Toggle + ventana horaria + brillo nocturno.
- **Ciudades** — 4 ciudades fijas con color picker, nombre, lat, lon. `$DATE` reemplaza el nombre por fecha.
- **Iconos** — Editor con paleta 16 colores, frames múltiples, ms por frame, botón ▶ Play para preview en navegador.
- **Otros** — Parpadeo del `:`, leading zero, formato de fecha (`$DATE` como `DD/MM` o `8 May`), indicador de proveedor meteo, segundera (off / marker / bar) con color y ancho, indicador de tendencia con umbrales.
- **Programaciones** — Hasta 10 entradas HH:MM → modo. La hora local usa el timezone de la primera ciudad. Repite cada día.
- **Colores hora y fecha (modos focus + Claude)** — Pickers para `focus_hour_color` y `focus_date_color`.
- **Auto-update** — Checkbox para activar/desactivar + intervalo en horas (1–720) + botón "Buscar update ahora".
- **Claude stats (modo 3)** — Campo `sessionKey` de claude.ai + intervalo de refresco.
- **Auto «hola»** — Toggle + hora (`<input type="time">`) + botón "Enviar «hola» ahora" + estado del último envío (poll cada 5 s). Debajo, toggle de **Keep awake**.
- **Jitter** (pestaña propia) — Estado BLE en vivo (poll cada 4 s), toggle `Jitter activo`, selector de intervalo y de paso máximo, botón "Aplicar". Los controles no se pisan por el poll mientras hay cambios sin aplicar.
- **Game of Life (modo 4)** — Color de células o toggle arcoíris + velocidad de simulación.
- **Imagen (modo 5)** — Selector de fichero; el navegador convierte a RGB565 64×23 y sube a `/api/userimg`.
- **Demoscene (modo 6)** — Selector de efecto (Llama / Plasma / Moiré / Nyan Cat) + paleta clásica o color custom.
- **Modo al arrancar** — Modo de display tras el boot.
- **Refresco meteo (segundos)** + selector de proveedor + claves API si aplica.
- **Logs meteo** — Tabla con offset/temp/code/day por ciudad + modal de debug por proveedor.
- **Backup / Restaurar** — Descarga JSON completo (sin creds WiFi) o carga uno previo.
- **Guardar / Recargar / Reiniciar device**.

## Botones (físicos y simulados)

La acción depende de si el [menú](#menú-en-pantalla) está abierto o no:

| Estado | Botón izq (A2) | Botón centro (A3) | Botón der (A4) |
|---|---|---|---|
| **Reloj** (menú cerrado) | Modo anterior | Abre el menú | Modo siguiente |
| **Menú principal** | Opción anterior | Entra en la opción | Opción siguiente |
| **Submenú, navegando** | Fila anterior | Entra a editar (o *Back* → sale) | Fila siguiente |
| **Submenú, editando** | Baja el valor | Acepta y persiste | Sube el valor |

> El brillo ya no está en izq/der directo: ahora vive en el submenú *Brightness*, que además muestra la barra con el `Level NN%`.

**Ripple**: en el reloj, cada pulsación dispara una onda blanca expandiéndose desde la posición del botón (~700 ms). Dentro del menú no hay ripple — el propio resaltado de la fila es la respuesta visual.

**Botones web**: `POST /api/button?b=left|center|right` o desde la pestaña "Botones" de la admin. Misma acción que los físicos, incluida la navegación del menú. **No** se ven afectados por la desactivación individual de cada TTP.

**Botón UP físico del board** (GPIO 6, mantener 3 s) → fuerza modo AP para reconfigurar WiFi sin tener que esperar a que falle STA.

## Auto-update via GitHub Releases

El firmware comprueba `https://api.github.com/repos/alberthorta/WorldTimeMatrixC/releases/latest` al boot y periódicamente. Si el `tag_name` remoto difiere de `FW_VERSION` local, descarga el `firmware.bin` adjunto y flashea sobre la marcha mostrando un splash con progreso.

- **Configuración desde la web**: checkbox `auto_update_enabled` + `auto_update_check_interval_h` (1–720).
- **Check manual**: botón "Buscar update ahora" → `POST /api/autoupdate/check`.
- Requiere repo público (GitHub no permite releases públicas en repos privados).
- Workflow para publicar release: `git tag vX.Y.Z && git push --tags && gh release create vX.Y.Z firmware.bin --notes ...`.

## Programaciones (cambio de modo)

Hasta 10 entradas (`schedule[0..9]`) configurables desde la web:

```json
{
  "enabled": true,
  "hour": 8,
  "minute": 0,
  "mode": 1
}
```

- `mode`: 0 = 4 filas, 1 = focus, 2 = Claude, 3 = Life, 4 = Imagen, 5 = Demoscene.
- Hora local (timezone de cities[0]).
- Dispara una vez por minuto en la transición — si el device estaba apagado a la hora exacta, no hay catchup hasta el día siguiente.
- Si la programación apunta a Claude pero no hay sessionKey, se salta.

## Estructura del proyecto

```
├── platformio.ini          # build config (envs: matrixportal_s3 y ota)
├── partitions_ota.csv      # 2 slots OTA de 3MB + LittleFS 1.4MB
├── data/
│   └── index.html          # placeholder; el HTML real está embebido
├── scripts/
│   └── version.py          # inyecta FW_VERSION desde git describe
├── src/
│   ├── main.cpp            # setup + loop con render @20fps + máquina de estados del menú
│   ├── Config.h/.cpp       # persistencia (LittleFS para cfg, NVS para wifi)
│   ├── WifiSetup.h/.cpp    # STA con fallback AP "WorldTime-Setup" + IP estática
│   ├── Display.h/.cpp      # HUB75-DMA + los 6 renders de modo + renderMenu
│   ├── Icons.h/.cpp        # 9 iconos × N frames, paleta 16 colores
│   ├── Weather.h/.cpp      # Open-Meteo + Tomorrow.io + WeatherAPI clients
│   ├── MoonPhase.h/.cpp    # cálculo de fase lunar
│   ├── ClaudeStats.h/.cpp  # client claude.ai/api/.../usage + openWindow ("hola")
│   ├── Jitter.h/.cpp       # ratón BLE HID (NimBLE) + servicio de control
│   ├── AutoUpdate.h/.cpp   # client GitHub releases + flash OTA
│   ├── WebApi.h/.cpp       # AsyncWebServer + todos los endpoints
│   ├── IndexHtml.h/.cpp    # UI completa embebida en flash
│   └── Version.h           # FW_VERSION (autogenerado por version.py)
├── mac/                    # app de barra de menú macOS para el jitter (Swift)
│   ├── Package.swift
│   ├── build.sh
│   └── Sources/WorldTimeJitter/main.swift
└── README.md
```

## API HTTP

| Endpoint | Método | Cuerpo | Descripción |
|---|---|---|---|
| `/` | GET | — | UI HTML embebida |
| `/api/status` | GET | — | IP, uptime, heap, psram, RSSI, modo WiFi |
| `/api/wifi` | GET | — | Modo (sta/ap/none), SSID actual, IP |
| `/api/wifi` | POST | `{ssid, password}` | Guarda creds en NVS y reinicia |
| `/api/wifi/scan` | GET | — | Lista de redes cercanas |
| `/api/config` | GET | — | Config completa JSON (sin wifi creds) |
| `/api/config` | POST | patch JSON | Aplica + persiste; si cambian cities, refetch meteo |
| `/api/config/export` | GET | — | Backup portable: solo el contenido de `cfg.json` |
| `/api/brightness` | POST | `{brightness?, night_brightness?}` | Live, sin escribir NVS |
| `/api/weather` | GET | — | Estado meteo runtime + diagnostics |
| `/api/weather/fetch?idx=N` | GET | — | Forzar fetch sincrono (debug) |
| `/api/weather/debug?idx=N&provider=...` | GET | — | URL + body raw del último fetch |
| `/api/firmware` | POST multipart | binario | OTA web (campo `firmware`) |
| `/api/button` | POST | `?b=left|center|right` | Simula pulsación del TTP correspondiente |
| `/api/jitter` | GET | — | Estado runtime del ratón BLE (`ble_connected`) + config actual |
| `/api/claude/hola` | GET | — | Estado del auto «hola»: `enabled`, hora, `last_date`, `status`, `error` |
| `/api/claude/hola` | POST | — | Dispara un «hola» ahora (400 si no hay sessionKey) |
| `/api/userimg` | POST multipart | binario | Imagen del modo 5: 2944 bytes RGB565 (64×23) |
| `/api/weather_provider` | GET / POST | `{provider, api_key}` | Proveedor meteo activo y sus claves |
| `/api/icons/preview/stop` | POST | — | Cancela el preview de icono en el panel |
| `/api/autoupdate/check` | POST | — | Dispara check ad-hoc de auto-update |
| `/api/rgb_order` | POST | `{rgb_order: "RGB"\|"RBG"}` | Cambia orden RGB y reinicia |
| `/api/reset` | POST | — | Reinicia el device |
| `/api/diag/nvs` | GET | — | Snapshot del estado de NVS + FS |
| `/api/icons/preview` | POST | frames | Preview de icono editado en el panel |
| `/debug/fs` | GET | — | Lista de ficheros LittleFS |

## Persistencia: NVS + LittleFS

**NVS (namespace `worldtime`)** — solo strings cortos:
- `wifi_ssid`, `wifi_pwd` — creds. **Nunca** aparecen en `/api/config` ni en backups.
- `boot_n` — contador de boots (instrumentación de diagnóstico).
- `rgb_order` — `RGB` o `RBG`. Identidad del panel, no portable entre devices.

**LittleFS (partition `littlefs`, 1.4 MB)** — JSON grandes y escrituras frecuentes:
- `/cfg.json` — toda la config: cities, brightness, palette, icons, modo noche, modo focus colors, claude config, auto «hola» + keep-awake, jitter, Life, Demoscene, auto-update, IP estática, schedule, etc.
- `/wxcache.json` — última meteo conocida.
- `/claudecache.json` — última stats de Claude conocida.
- `/userimg.bin` — imagen del modo 5 en RGB565 crudo (2944 bytes).
- `/index.html` — UI servida; sembrado al boot desde `IndexHtml.cpp` si falta.

**Por qué LittleFS en vez de NVS para los blobs grandes**: NVS fragmenta con saves frecuentes de blobs (cada `putBytes` deja entries marcadas deleted, el GC solo reclama páginas full-deleted). Tras decenas de saves la partición se satura, `putBytes` falla silente y los reboots cargan defaults — bug histórico de "OTA reseta config". LittleFS aguanta KBs y miles de re-escrituras sin degradación.

## Setup desde cero

### Requisitos

```bash
brew install platformio       # macOS
# o pipx install platformio
```

### Primer flash via USB

```bash
cd WorldTimeMatrixC
pio run -e matrixportal_s3 -t upload
```

~20 s. Tras el flash la placa reinicia. Sin creds WiFi en NVS, levanta el AP `WorldTime-Setup` (pwd `matrixportal`).

### Configurar WiFi

1. Conecta tu Mac/móvil al WiFi `WorldTime-Setup`.
2. Abre `http://192.168.4.1/`.
3. Busca redes → selecciona → password → "Conectar y reiniciar".
4. La placa reinicia, conecta a tu WiFi.

### Encontrar la IP

Cada device se anuncia como `WorldTimeXXX.local` donde XXX es el último octeto de su IP. O escanea por mDNS o ARP:

```bash
ping -c 1 WorldTime62.local            # si conoces el octeto
# o por LAN
for ip in 192.168.X.{50..120}; do
  curl -sS -m 1 "http://$ip/api/status" 2>/dev/null | grep -q wifi_mode && echo "$ip"
done
```

## Despliegue / actualización

### 1. USB (desarrollo)

```bash
pio run -e matrixportal_s3 -t upload
```

### 2. ArduinoOTA (espota)

El env `ota` en `platformio.ini` está configurado contra una IP específica:

```bash
pio run -e ota -t upload
# o con override
pio run -e ota -t upload --upload-port 192.168.X.Y
```

Caveat: en macOS el firewall bloquea el puerto entrante temporal. Permitir Python en Firewall.

### 3. OTA web (multipart)

```bash
curl -X POST -F "firmware=@.pio/build/matrixportal_s3/firmware.bin" \
     http://<ip>/api/firmware
```

Más robusto que ArduinoOTA. La conexión puede fallar con RSSI < −78; reintentar suele funcionar. En tres devices conocidos: latencia y fiabilidad escalan con la cobertura WiFi.

**Si el upload se corta a mitad** (`curl` exit 55/56 a los ~50-60 s), reinicia el device *antes* de reintentar:

```bash
curl -X POST http://<ip>/api/reset && sleep 25
curl -F "firmware=@.pio/build/ota/firmware.bin" http://<ip>/api/firmware
```

Cada OTA abortado deja estado colgado y el heap libre va cayendo (~84 KB en boot limpio → ~75 KB tras dos intentos fallidos). Reintentar sin reset tiende a fallar otra vez; con el heap recién arrancado suele entrar a la primera.

### 4. Auto-update via release de GitHub (producción)

```bash
git tag v0.X.Y
git push --tags
pio run -e matrixportal_s3        # build con FW_VERSION limpio
(cd mac && ./build.sh --zip)      # build de la app con la misma versión
gh release create v0.X.Y \
   .pio/build/matrixportal_s3/firmware.bin \
   mac/WorldTimeJitter.zip \
   --title "..." --notes "..."
```

**Ojo al orden**: tanto `version.py` como `mac/build.sh` sacan la versión de `git describe`, así que hay que **taggear antes de compilar** o los binarios saldrán con la versión anterior (o con sufijo `-dirty`).

Los devices con `auto_update_enabled` activo cogerán el firmware al próximo boot o al check periódico; la app de macOS cogerá el `.zip` en su siguiente chequeo.

## Backup / restore de config

```bash
# Descargar backup portable (solo cfg.json, sin wifi ni rgb_order)
curl http://<ip>/api/config/export > backup.json

# Restaurar (sobre /api/config con patch)
curl -X POST -H "Content-Type: application/json" \
     --data-binary @backup.json http://<ip>/api/config
```

## Limitaciones conocidas

1. **HTTP OTA con RSSI marginal**: la conexión TCP puede romperse a mitad del upload con < −78 dBm. Reintentar o usar auto-update via GitHub (HTTPClient interno aguanta mejor).
2. **USB CDC serial flaky**: `pio device monitor` no siempre captura el boot. Debug por endpoints HTTP (`/api/status`, `/api/diag/nvs`).
3. **Cities fijas (4)**: la versión Python soportaba lista variable; aquí siempre 4.
4. **Iconos hardcoded por nombre**: las 9 categorías (`SUN`, `PARTLY`, `CLOUD`, `RAIN`, `SNOW`, `STORM`, `FOG`, `MOON`, `PARTLY_NIGHT`) están fijadas. Para añadir/eliminar tipos hay que tocar `Icons.cpp` + `Display::IconType` + `Weather::iconForCode`.
5. **Schedule sin catchup**: si el device estaba apagado a la hora programada, no se dispara hasta el siguiente día.
6. **Claude sessionKey**: es la cookie de claude.ai, no una API key oficial. Caduca cada cierto tiempo y hay que renovarla.
7. **Jitter BLE solo en modo STA**: el controlador BT del ESP32-S3 time-sharea la radio con la WiFi vía modem-sleep. El proyecto arranca con `WiFi.setSleep(false)` para bajar la latencia HTTP, pero con el modem-sleep desactivado el coex **aborta en `coex_enable` y deja el device en boot-loop**. `Jitter::begin()` lo evita: solo levanta BLE si el modo es STA, y hace `WiFi.setSleep(true)` antes de `NimBLEDevice::init`. En modo AP el jitter queda en pausa (tampoco hace falta durante el setup de WiFi). Si tocas esto y provocas un boot-loop, la única salida es reflashear por USB.
8. **Modo de display no persiste**: `g_displayMode` vive solo en RAM. Tras un reboot se vuelve a `startup_mode`.

## Recuperación de emergencia

- **Reset por USB**: pulsa el botón RESET físico.
- **Forzar AP**: mantener el botón UP físico del board (GPIO 6) 3 s desde la admin → entra en modo AP `WorldTime-Setup`.
- **Recovery via AP**: si las creds WiFi están corruptas o no conecta, al boot levanta el AP. Conéctate y reconfigura.
- **IP estática rota**: si seteaste IP estática inválida, el firmware hace fallback a DHCP automáticamente. Si aun así no conecta, modo AP por hold de UP.
- **Wipe completo**: `pio run -t erase` borra TODO incluido NVS — perderás creds WiFi. Tras eso, USB upload y reconfigurar desde AP.

## Comparativa con `firmware/` (CircuitPython)

| | CircuitPython | C++ (este repo) |
|---|---|---|
| Hot-reload | Edita 1 fichero, Ctrl-D | Build + flash (~20 s) o auto-update |
| Animaciones bajo carga HTTP | Pausa visible | Smooth (DMA + tasks) |
| Brillo | Reescalado de paletas (CPU) | `setBrightness8()` (hardware PWM) |
| Color depth | 4-bit | 12-bit |
| Modos display | 1 (4 filas) | 6 (4 filas / focus / Claude / Life / imagen / demoscene) |
| REPL | `/cp/serial/` WS | No (Serial USB CDC, flaky) |
| Edición remota config | `/api/config` POST | `/api/config` POST |
| Edición remota código | `PUT /fs/code.py` | Auto-update via GitHub Release |
| Sensores táctiles | No | 3 TTP223 + simulados web |
| Mascot animado | No | Clawd con look / blink / happy / waving |
| Schedule | No | Hasta 10 programaciones HH:MM → modo |
| IP estática | No | Configurable desde web |
| Menú en pantalla | No | Overlay navegable con los 3 botones |
| Ratón BLE (jitter) | No | NimBLE HID + app de barra de menú macOS |

## Convenciones

- Comentarios en español, código en inglés (variables, funciones, identificadores).
- Mensajes de commit en voz del usuario, sin atribución a AI.
- Antes de tocar el device .62 (producción), preguntar; en .51/.36 (dev) más libre.
- Verificar build (`pio run`) antes de flashear.
- Cuando algo cambie comportamiento visible, decir al usuario qué esperar para que pueda verificar.
