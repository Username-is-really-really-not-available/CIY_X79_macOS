#!/bin/bash
set -e
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${PROJECT_ROOT}"

APP_NAME="CIYX79Control"
VERSION="${1:-${VERSION:-1.4.1}}"
VERSION="${VERSION#v}"
if [[ ! "${VERSION}" =~ ^[0-9A-Za-z][0-9A-Za-z._-]*$ ]]; then
    echo "FEHLER: Ungültige Version '${VERSION}'. Erlaubt sind Buchstaben, Ziffern, Punkt, Unterstrich und Bindestrich." >&2
    exit 1
fi
DIST_DIR="${PROJECT_ROOT}/dist"
APP_BUNDLE="${DIST_DIR}/${APP_NAME}.app"
MACOS_DIR="${APP_BUNDLE}/Contents/MacOS"
RESOURCES_DIR="${APP_BUNDLE}/Contents/Resources"

echo "=== Erstelle ${APP_NAME}.app v${VERSION} (Universal Binary) ==="

mkdir -p "${DIST_DIR}"
mkdir -p "${MACOS_DIR}"
mkdir -p "${RESOURCES_DIR}"

echo "1. Kompiliere Universal Binary (arm64 & x86_64) mit Clang..."
clang -arch arm64 -arch x86_64 \
    -mmacosx-version-min=12.0 \
    -O2 \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -UCIY_ENABLE_TEST_HOOKS \
    -Wall -Wextra -Werror \
    -fobjc-arc \
    -framework Cocoa \
    -framework IOKit \
    -framework CoreFoundation \
    -framework UniformTypeIdentifiers \
    src/core/ciy_hid.c \
    src/layouts/ciy_x79_iso_de.c \
    src/app/KeyboardBackend.m \
    src/app/CIYKeyboardView.m \
    src/app/CIYBatteryMonitor.m \
    src/app/CIYLocalization.m \
    src/app/AppDelegate.m \
    src/app/main.m \
    -o "${MACOS_DIR}/${APP_NAME}"

echo "2. Kompiliere CLI-Tool ciy_ctl (Universal Binary)..."
clang -arch arm64 -arch x86_64 \
    -mmacosx-version-min=12.0 \
    -O2 \
    -fstack-protector-strong \
    -D_FORTIFY_SOURCE=2 \
    -UCIY_ENABLE_TEST_HOOKS \
    -Wall -Wextra -Werror \
    -framework IOKit \
    -framework CoreFoundation \
    src/core/ciy_hid.c \
    src/layouts/ciy_x79_iso_de.c \
    src/cli/ciy_ctl.c \
    -o "${DIST_DIR}/ciy_ctl"

echo "3. Kopiere AppIcon, Dokumentation, Lokalisierung & erstelle Info.plist..."
if [ -f "resources/AppIcon.icns" ]; then
    cp "resources/AppIcon.icns" "${RESOURCES_DIR}/AppIcon.icns"
fi
for doc in README.md README_EN.md; do
    if [ ! -f "${doc}" ]; then
        echo "FEHLER: ${doc} fehlt." >&2
        exit 1
    fi
    cp "${doc}" "${RESOURCES_DIR}/"
done

for lproj in en de; do
    strings_file="resources/${lproj}.lproj/Localizable.strings"
    if [ ! -f "${strings_file}" ]; then
        echo "FEHLER: ${strings_file} fehlt." >&2
        exit 1
    fi
    rm -rf "${RESOURCES_DIR}/${lproj}.lproj"
    cp -R "resources/${lproj}.lproj" "${RESOURCES_DIR}/"
done

cat << EOF > "${APP_BUNDLE}/Contents/Info.plist"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>CIYX79Control</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundleIdentifier</key>
    <string>com.juliengenau.ciy-x79-control</string>
    <key>CFBundleLocalizations</key>
    <array>
        <string>en</string>
        <string>de</string>
    </array>
    <key>CFBundleName</key>
    <string>CIY X79 Control</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSRequiresAquaSystemAppearance</key>
    <false/>
</dict>
</plist>
EOF

chmod +x "${MACOS_DIR}/${APP_NAME}"
chmod +x "${DIST_DIR}/ciy_ctl"

for bin in "${MACOS_DIR}/${APP_NAME}" "${DIST_DIR}/ciy_ctl"; do
    if strings "${bin}" | grep -qE 'CIY_TESTING|CIY_FAIL_ON_HARDWARE_IO|CIY_BATTERY_MOCK'; then
        echo "FEHLER: Test-Hook-Strings im Release-Binary: ${bin}" >&2
        exit 1
    fi
done

CODESIGN_IDENTITY="${CODESIGN_IDENTITY:--}"
echo "4. Codesign (${CODESIGN_IDENTITY}, arm64 & x86_64)..."
codesign --force --deep --options runtime --sign "${CODESIGN_IDENTITY}" "${APP_BUNDLE}"
codesign --force --options runtime --sign "${CODESIGN_IDENTITY}" "${DIST_DIR}/ciy_ctl"

# Praktische Symlinks im Root-Verzeichnis aktualisieren
ln -sfn "dist/${APP_NAME}.app" "${PROJECT_ROOT}/${APP_NAME}.app"
ln -sfn "dist/ciy_ctl" "${PROJECT_ROOT}/ciy_ctl"

echo "=== Fertig! ${APP_BUNDLE} und ciy_ctl wurden erfolgreich erstellt. ==="
