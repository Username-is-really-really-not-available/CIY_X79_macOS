#!/bin/bash
set -e
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${PROJECT_ROOT}"

APP_NAME="CIYX79Control"
DIST_DIR="${PROJECT_ROOT}/dist"
APP_BUNDLE="${DIST_DIR}/${APP_NAME}.app"
MACOS_DIR="${APP_BUNDLE}/Contents/MacOS"
RESOURCES_DIR="${APP_BUNDLE}/Contents/Resources"

echo "=== Erstelle ${APP_NAME}.app (Universal Binary) ==="

mkdir -p "${DIST_DIR}"
mkdir -p "${MACOS_DIR}"
mkdir -p "${RESOURCES_DIR}"

echo "1. Kompiliere Universal Binary (arm64 & x86_64) mit Clang..."
clang -arch arm64 -arch x86_64 \
    -Wall -Wextra \
    -fobjc-arc \
    -framework Cocoa \
    -framework IOKit \
    -framework CoreFoundation \
    -framework UniformTypeIdentifiers \
    src/core/ciy_hid.c \
    src/app/KeyboardBackend.m \
    src/app/CIYKeyboardView.m \
    src/app/AppDelegate.m \
    src/app/main.m \
    -o "${MACOS_DIR}/${APP_NAME}"

echo "2. Kompiliere CLI-Tool ciy_ctl (Universal Binary)..."
clang -arch arm64 -arch x86_64 \
    -Wall -Wextra \
    -framework IOKit \
    -framework CoreFoundation \
    src/core/ciy_hid.c \
    src/cli/ciy_ctl.c \
    -o "${DIST_DIR}/ciy_ctl"

echo "3. Kopiere AppIcon, Dokumentation & erstelle Info.plist..."
if [ -f "resources/AppIcon.icns" ]; then
    cp "resources/AppIcon.icns" "${RESOURCES_DIR}/AppIcon.icns"
fi
cp README*.md "${RESOURCES_DIR}/" 2>/dev/null || true

cat << 'EOF' > "${APP_BUNDLE}/Contents/Info.plist"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>CIYX79Control</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundleIdentifier</key>
    <string>com.juliengenau.ciy-x79-control</string>
    <key>CFBundleName</key>
    <string>CIY X79 Control</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
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

echo "4. Ad-hoc Codesign (arm64 & x86_64)..."
codesign --force --deep --sign - "${APP_BUNDLE}"
codesign --force --sign - "${DIST_DIR}/ciy_ctl"

# Praktische Symlinks im Root-Verzeichnis aktualisieren
ln -sfn "dist/${APP_NAME}.app" "${PROJECT_ROOT}/${APP_NAME}.app"
ln -sfn "dist/ciy_ctl" "${PROJECT_ROOT}/ciy_ctl"

echo "=== Fertig! ${APP_BUNDLE} und ciy_ctl wurden erfolgreich erstellt. ==="
