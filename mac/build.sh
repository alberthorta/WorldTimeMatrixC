#!/bin/bash
# Compila WorldTimeJitter y lo empaqueta como app de barra de menu (.app).
set -e
cd "$(dirname "$0")"

echo "==> Compilando (release)…"
swift build -c release

APP="WorldTimeJitter.app"
BIN=".build/release/WorldTimeJitter"

echo "==> Empaquetando $APP…"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cp "$BIN" "$APP/Contents/MacOS/WorldTimeJitter"
cp Info.plist "$APP/Contents/Info.plist"

# Firma ad-hoc para que TCC asocie el permiso de Bluetooth a la app.
codesign --force --deep --sign - "$APP"

echo "==> Hecho: $APP"
echo "    Ejecuta:  open $APP     (o ./$APP/Contents/MacOS/WorldTimeJitter para ver logs)"
