#!/usr/bin/env bash
# Wrapper de PlatformIO para los flujos típicos de este proyecto.
#
# Uso:
#   ./build.sh                  # compila (no sube)
#   ./build.sh usb              # compila y sube por USB (al device conectado)
#   ./build.sh ota <ip>         # compila y sube via Web OTA (multipart)
#   ./build.sh espota <ip>      # compila y sube via ArduinoOTA (espota.py)
#   ./build.sh monitor          # serial monitor (115200, USB)
#   ./build.sh clean            # pio run -t clean
#
# Web OTA es más fiable que ArduinoOTA con WiFi marginal.

set -euo pipefail

ENV="matrixportal_s3"
BIN=".pio/build/${ENV}/firmware.bin"

cmd_build() {
    pio run -e "$ENV"
}

cmd_usb() {
    pio run -e "$ENV" -t upload
}

cmd_ota() {
    local ip="${1:-}"
    if [[ -z "$ip" ]]; then
        echo "Falta IP. Uso: $0 ota <ip>" >&2
        exit 1
    fi
    cmd_build
    echo "→ Subiendo $BIN a http://$ip/api/firmware ..."
    curl -fS --max-time 240 \
        -F "firmware=@${BIN}" \
        "http://${ip}/api/firmware" \
        -w "\nHTTP %{http_code} en %{time_total}s\n"
}

cmd_espota() {
    local ip="${1:-}"
    if [[ -z "$ip" ]]; then
        echo "Falta IP. Uso: $0 espota <ip>" >&2
        exit 1
    fi
    pio run -e ota -t upload --upload-port "$ip"
}

cmd_monitor() {
    pio device monitor -e "$ENV"
}

cmd_clean() {
    pio run -e "$ENV" -t clean
}

case "${1:-build}" in
    build)   cmd_build ;;
    usb)     cmd_usb ;;
    ota)     shift; cmd_ota "$@" ;;
    espota)  shift; cmd_espota "$@" ;;
    monitor) cmd_monitor ;;
    clean)   cmd_clean ;;
    -h|--help)
        sed -n '2,15p' "$0"   # imprime el header de ayuda
        ;;
    *)
        echo "Comando desconocido: $1" >&2
        sed -n '2,15p' "$0" >&2
        exit 2
        ;;
esac
