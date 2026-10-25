#!/bin/bash
set -e
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${PROJECT_ROOT}"

APP_NAME="CIYX79Control"
VERSION="${1:-${VERSION:-1.0.2}}"
VERSION="${VERSION#v}"
DIST_DIR="${PROJECT_ROOT}/dist"
APP_BUNDLE="${DIST_DIR}/${APP_NAME}.app"
DMG_NAME="${DIST_DIR}/${APP_NAME}-${VERSION}.dmg"
STAGING_DIR="/tmp/ciy_dmg_staging_$$"

trap 'rm -rf "${STAGING_DIR}"' EXIT INT TERM

echo "=== Erstelle ${APP_NAME}-${VERSION}.dmg ==="

# 1. Sicherstellen, dass App und CLI-Tool frisch kompiliert und signiert sind
if [ ! -d "${APP_BUNDLE}" ] || [ ! -f "${DIST_DIR}/ciy_ctl" ]; then
    echo "Baue ${APP_BUNDLE} und ciy_ctl zuerst..."
    "${PROJECT_ROOT}/scripts/build_app.sh" "${VERSION}"
fi

# 2. Bereite Staging-Verzeichnis vor
echo "1. Bereite DMG-Inhalt vor..."
rm -rf "${STAGING_DIR}"
mkdir -p "${STAGING_DIR}"

cp -R "${APP_BUNDLE}" "${STAGING_DIR}/"
cp "${DIST_DIR}/ciy_ctl" "${STAGING_DIR}/"
cp README*.md "${STAGING_DIR}/" 2>/dev/null || true
cp "LICENSE" "${STAGING_DIR}/" 2>/dev/null || true
ln -s /Applications "${STAGING_DIR}/Applications"

# 3. Erstelle komprimiertes Disk Image (UDZO) mit nativem hdiutil
echo "2. Erstelle komprimiertes Disk Image (hdiutil)..."
rm -f "${DMG_NAME}"
hdiutil create \
    -volname "CIY X79 Control" \
    -srcfolder "${STAGING_DIR}" \
    -ov \
    -format UDZO \
    "${DMG_NAME}"

# 4. Aufräumen
rm -rf "${STAGING_DIR}"

# Symlink im Root-Verzeichnis bereitstellen
ln -sfn "dist/${APP_NAME}-${VERSION}.dmg" "${PROJECT_ROOT}/${APP_NAME}-${VERSION}.dmg"

echo "3. DMG erfolgreich erstellt: ${DMG_NAME}"
echo "Dateigröße: $(du -sh "${DMG_NAME}" | cut -f1)"
echo "SHA256: $(shasum -a 256 "${DMG_NAME}" | cut -d' ' -f1)"
echo "=== Fertig! ==="
