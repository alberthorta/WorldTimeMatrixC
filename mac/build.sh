#!/bin/bash
# Compila WorldTimeJitter y lo empaqueta como app de barra de menu (.app).
#
# Con --zip genera ademas WorldTimeJitter.zip, que es el asset que la propia app
# descarga al autoactualizarse (ver Sources/WorldTimeJitter/UpdateInstaller.swift).
#
# La version sale de `git describe`, igual que FW_VERSION en el firmware: la app
# y el firmware comparten tag porque el protocolo BLE tiene que cuadrar entre los
# dos. CFBundleShortVersionString lleva el tag limpio (lo que se compara contra
# la release de GitHub) y CFBundleVersion el describe completo (para depurar de
# que commit salio un build).
set -e
cd "$(dirname "$0")"

APP="WorldTimeJitter.app"
BIN=".build/release/WorldTimeJitter"

# Version: tag limpio + describe completo. Sin tags todavia -> 0.0.0.
DESCRIBE="$(git describe --tags --always --dirty 2>/dev/null || echo unknown)"
TAG="$(git describe --tags --abbrev=0 2>/dev/null || echo v0.0.0)"
SHORT_VERSION="${TAG#v}"

echo "==> Compilando (release)…"
swift build -c release

echo "==> Empaquetando $APP (version $SHORT_VERSION, build $DESCRIBE)…"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cp "$BIN" "$APP/Contents/MacOS/WorldTimeJitter"
cp Info.plist "$APP/Contents/Info.plist"

# Sobrescribimos la version en la COPIA del bundle, no en el Info.plist fuente:
# asi el repo no acumula churn de version en cada build.
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $SHORT_VERSION" "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $DESCRIBE" "$APP/Contents/Info.plist"

# Firma ad-hoc para que TCC asocie el permiso de Bluetooth a la app.
codesign --force --deep --sign - "$APP"

echo "==> Hecho: $APP"
echo "    Ejecuta:  open $APP     (o ./$APP/Contents/MacOS/WorldTimeJitter para ver logs)"

if [[ "${1:-}" == "--zip" ]]; then
    ZIP="WorldTimeJitter.zip"
    echo "==> Generando $ZIP para la release…"
    rm -f "$ZIP"
    # ditto -c -k --keepParent: preserva la estructura del bundle (symlinks,
    # permisos, firma). Un `zip -r` normal la rompe y el .app no arranca.
    ditto -c -k --keepParent "$APP" "$ZIP"
    echo "    Adjuntalo a la release:"
    echo "    gh release upload vX.Y.Z mac/$ZIP"
fi
