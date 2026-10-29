#!/bin/bash
set -e

export CIY_TESTING="${CIY_TESTING:-1}"

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ciy_ctl_test.XXXXXX")"
cleanup() {
    rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

if [ -n "${CIY_CTL_OVERRIDE:-}" ]; then
    CIY_CTL="${CIY_CTL_OVERRIDE}"
    if [ ! -x "${CIY_CTL}" ]; then
        echo "FEHLER: CIY_CTL_OVERRIDE ist nicht ausführbar: ${CIY_CTL}"
        exit 1
    fi
else
    CIY_CTL="${TMP_DIR}/ciy_ctl"
    CLANG_FLAGS=(-Wall -Wextra -mmacosx-version-min=12.0)
    if [ "${CIY_SANITIZE:-0}" = "1" ]; then
        CLANG_FLAGS+=(-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    fi
    echo "Kompiliere ciy_ctl für Test..."
    clang "${CLANG_FLAGS[@]}" -framework IOKit -framework CoreFoundation \
        "${PROJECT_ROOT}/src/core/ciy_hid.c" \
        "${PROJECT_ROOT}/src/layouts/ciy_x79_iso_de.c" \
        "${PROJECT_ROOT}/src/cli/ciy_ctl.c" \
        -o "${CIY_CTL}"
fi

echo "=== Test CLI: 1. Hilfe & Optionen ==="
HELP_OUT="$("${CIY_CTL}" --help)"
echo "${HELP_OUT}" | grep -q "Verwendung:"
echo "${HELP_OUT}" | grep -q -- "-b, --brightness"
echo "${HELP_OUT}" | grep -q -- "--json"
echo "${HELP_OUT}" | grep -q -- "--dump-bytes"
echo "${HELP_OUT}" | grep -q "export-layout"

echo "=== Test CLI: 2. list-modes (Text & JSON) ==="
MODES_TXT="$("${CIY_CTL}" list-modes)"
echo "${MODES_TXT}" | grep -q "Wave"
MODES_JSON="$("${CIY_CTL}" list-modes --json)"
echo "${MODES_JSON}" | grep -q '"modes":'
echo "${MODES_JSON}" | grep -q '"colors":'

echo "=== Test CLI: 2b. validate-profile (Hardware-frei) ==="
# Valide Profile (Text & JSON)
VAL_TXT="$("${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/valid_profile.json")"
echo "${VAL_TXT}" | grep -q "ist gültig"
VAL_JSON="$("${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/valid_profile.json" --json)"
echo "${VAL_JSON}" | grep -q '"valid": true'
echo "${VAL_JSON}" | grep -q '"profiles": 1'
echo "${VAL_JSON}" | grep -q '"keys": 3'

# Optionsreihenfolge: --json vor validate-profile
VAL_JSON_ORDER="$("${CIY_CTL}" --json validate-profile "${PROJECT_ROOT}/tests/fixtures/valid_profile.json")"
echo "${VAL_JSON_ORDER}" | grep -q '"valid": true'

# Multi-Profil
VAL_MULTI_JSON="$("${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/valid_multi_profile.json" --json)"
echo "${VAL_MULTI_JSON}" | grep -q '"valid": true'
echo "${VAL_MULTI_JSON}" | grep -q '"profiles": 2'

# Ungültige Profile: Müssen fehlschlagen (Exit-Code != 0)
set +e
"${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/invalid_slot_range.json" 2>/dev/null
if [ $? -eq 0 ]; then
    echo "FEHLER: validate-profile akzeptierte ungültigen Slot!"
    exit 1
fi

BAD_SLOT_JSON="$("${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/invalid_slot_range.json" --json)"
echo "${BAD_SLOT_JSON}" | grep -q '"valid": false'
echo "${BAD_SLOT_JSON}" | grep -q '"field": "130"'

"${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/invalid_color.json" 2>/dev/null
if [ $? -eq 0 ]; then
    echo "FEHLER: validate-profile akzeptierte ungültige Farbe!"
    exit 1
fi

BAD_COLOR_JSON="$("${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/invalid_color.json" --json)"
echo "${BAD_COLOR_JSON}" | grep -q '"valid": false'
echo "${BAD_COLOR_JSON}" | grep -q '"field": "14"'

"${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/invalid_syntax.json" 2>/dev/null
if [ $? -eq 0 ]; then
    echo "FEHLER: validate-profile akzeptierte korruptes JSON!"
    exit 1
fi

BAD_SYNTAX_JSON="$("${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/invalid_syntax.json" --json)"
echo "${BAD_SYNTAX_JSON}" | grep -q '"valid": false'

# Fehlendes Argument
"${CIY_CTL}" validate-profile 2>/dev/null
if [ $? -eq 0 ]; then
    echo "FEHLER: validate-profile ohne Argument akzeptiert!"
    exit 1
fi
set -e

echo "=== Test CLI: 2c. --dry-run (Hardware-frei & Zero-I/O Garantie) ==="
# 1. Textausgabe gemäß Release-Spezifikation v1.1.0
DRY_TXT="$("${CIY_CTL}" --dry-run load "${PROJECT_ROOT}/tests/fixtures/valid_profile.json")"
echo "${DRY_TXT}" | grep -q "DRY RUN — no USB device opened; no HID transfers sent"
echo "${DRY_TXT}" | grep -q "Device profile: CIY X79 (VID 0x258A, PID 0x0049, interface 1)"
echo "${DRY_TXT}" | grep -q "Profile: Valid Test Profile"
echo "${DRY_TXT}" | grep -q "Plan: Report 0x0B (379 bytes) -> wait 50 ms -> Report 0x0A (42 bytes, mode 19, brightness 5)"
echo "${DRY_TXT}" | grep -q "Result: valid; hardware unchanged"

# 2. JSON-Ausgabe
DRY_JSON="$("${CIY_CTL}" --dry-run load "${PROJECT_ROOT}/tests/fixtures/valid_profile.json" -b 4 --json)"
echo "${DRY_JSON}" | grep -q '"dry_run": true'
echo "${DRY_JSON}" | grep -q '"hardware_opened": false'
echo "${DRY_JSON}" | grep -q '"report_id": "0x0B"'
echo "${DRY_JSON}" | grep -q '"bytes": 379'
echo "${DRY_JSON}" | grep -q '"delay_ms": 50'
echo "${DRY_JSON}" | grep -q '"report_id": "0x0A"'
echo "${DRY_JSON}" | grep -q '"bytes": 42'
echo "${DRY_JSON}" | grep -q '"brightness": 4'
echo "${DRY_JSON}" | grep -q '"result": "valid; hardware unchanged"'

# 3. Automatisierter Nachweis: Zero I/O via CIY_FAIL_ON_HARDWARE_IO Guard
CIY_FAIL_ON_HARDWARE_IO=1 "${CIY_CTL}" --dry-run load "${PROJECT_ROOT}/tests/fixtures/valid_profile.json" > /dev/null
CIY_FAIL_ON_HARDWARE_IO=1 "${CIY_CTL}" --dry-run load "${PROJECT_ROOT}/tests/fixtures/valid_profile.json" --json > /dev/null

# 4. Ungültiges Profil im Dry-Run MUSS fehlschlagen (Exit-Code != 0)
set +e
"${CIY_CTL}" --dry-run load "${PROJECT_ROOT}/tests/fixtures/invalid_slot_range.json" 2>/dev/null
if [ $? -eq 0 ]; then
    echo "FEHLER: --dry-run load akzeptierte ungültiges Profil!"
    exit 1
fi

DRY_BAD_JSON="$("${CIY_CTL}" --dry-run load "${PROJECT_ROOT}/tests/fixtures/invalid_slot_range.json" --json)"
echo "${DRY_BAD_JSON}" | grep -q '"success": false'
echo "${DRY_BAD_JSON}" | grep -q '"field": "130"'

# 5. --dump-bytes Inspektion (Text & JSON)
DRY_HEX="$("${CIY_CTL}" --dry-run load "${PROJECT_ROOT}/tests/fixtures/valid_profile.json" --dump-bytes)"
echo "${DRY_HEX}" | grep -q "Raw Wire Bytes (Hex):"
echo "${DRY_HEX}" | grep -q "Report 0x0B"
DRY_HEX_JSON="$("${CIY_CTL}" --dry-run load "${PROJECT_ROOT}/tests/fixtures/valid_profile.json" --dump-bytes --json)"
echo "${DRY_HEX_JSON}" | grep -q '"hex": "0b'

# 6. Universeller --dry-run für Schreibbefehle (fill, set, wasd)
DRY_FILL="$("${CIY_CTL}" --dry-run fill "#FF0000" -b 3)"
echo "${DRY_FILL}" | grep -q "Vollfarbe #FF0000"
echo "${DRY_FILL}" | grep -q "brightness 3"
CIY_FAIL_ON_HARDWARE_IO=1 "${CIY_CTL}" --dry-run fill "#00FF00" > /dev/null

DRY_SET="$("${CIY_CTL}" --dry-run set 15 0 2 4)"
echo "${DRY_SET}" | grep -q "mode 15, color 0, speed 2, brightness 4"
CIY_FAIL_ON_HARDWARE_IO=1 "${CIY_CTL}" --dry-run set 1 0 1 5 > /dev/null

DRY_WASD="$("${CIY_CTL}" --dry-run wasd)"
echo "${DRY_WASD}" | grep -q "WASD Gaming"
CIY_FAIL_ON_HARDWARE_IO=1 "${CIY_CTL}" --dry-run wasd > /dev/null

# 7. --dry-run mit nicht-unterstütztem Befehl (z. B. validate-profile) muss fehlschlagen
DRY_VAL_ERR="$("${CIY_CTL}" --dry-run validate-profile "${PROJECT_ROOT}/tests/fixtures/valid_profile.json" 2>&1 || true)"
echo "${DRY_VAL_ERR}" | grep -q "nur für Schreibbefehle"
set -e

echo "=== Test CLI: 2d. export-layout (Text & JSON) ==="
LAYOUT_TXT="$("${CIY_CTL}" export-layout)"
echo "${LAYOUT_TXT}" | grep -q "Esc"
echo "${LAYOUT_TXT}" | grep -q "R-Byte"
echo "${LAYOUT_TXT}" | grep -q "ciy_x79_iso_de"

LAYOUT_JSON="$("${CIY_CTL}" export-layout --json)"
echo "${LAYOUT_JSON}" | grep -q '"layout_id": "ciy_x79_iso_de"'
echo "${LAYOUT_JSON}" | grep -q '"key_count": 69'
echo "${LAYOUT_JSON}" | grep -q '"surface_count": 70'
echo "${LAYOUT_JSON}" | grep -q '"report_0b_r_offset": 3'
echo "${LAYOUT_JSON}" | grep -q '"report_0b_g_offset": 129'
echo "${LAYOUT_JSON}" | grep -q '"report_0b_b_offset": 255'

# Hardware-Freiheit garantieren
CIY_FAIL_ON_HARDWARE_IO=1 "${CIY_CTL}" export-layout > /dev/null
CIY_FAIL_ON_HARDWARE_IO=1 "${CIY_CTL}" export-layout --json > /dev/null

echo "=== Test CLI: 3. Fehlerbehandlung ungültige Optionen & Helligkeit ==="
set +e
"${CIY_CTL}" --unknown-option 2>/dev/null
if [ $? -eq 0 ]; then
    echo "FEHLER: Unbekannte Option wurde akzeptiert!"
    exit 1
fi

ERR_JSON="$("${CIY_CTL}" --unknown-option --json)"
echo "${ERR_JSON}" | grep -q "Unbekannte Option"

"${CIY_CTL}" -b 9 status 2>/dev/null
if [ $? -eq 0 ]; then
    echo "FEHLER: Ungültige Helligkeit 9 wurde akzeptiert!"
    exit 1
fi

BRIGHT_ERR_JSON="$("${CIY_CTL}" -b 9 --json status)"
echo "${BRIGHT_ERR_JSON}" | grep -q "Ungültige Helligkeit"
set -e

echo "=== Test CLI: 4. ciy_ctl load Argument-Auflösung (Finding 5) ==="
TMP_JSON="${TMP_DIR}/profiles.json"
cat << 'EOF' > "${TMP_JSON}"
[
  {
    "name": "First Profile",
    "slots": { "14": "#FF0000" }
  },
  {
    "name": "Second Profile",
    "slots": { "21": "#00FF00" }
  }
]
EOF

# 4.1 Numerischer Profilindex "2" darf NICHT als Helligkeit interpretiert werden!
# Wenn Tastatur verbunden ist, wendet der Befehl Profil 2 an.
# Die JSON-Ausgabe muss name "Second Profile" melden:
LOAD_OUT="$("${CIY_CTL}" load "${TMP_JSON}" 2 --json)"
echo "${LOAD_OUT}" | grep -q '"name": "Second Profile"'
echo "${LOAD_OUT}" | grep -q '"brightness": 5'

# 4.2 Kombination mit -b Flag: Profil 2 mit Helligkeit 3
LOAD_OUT2="$("${CIY_CTL}" load "${TMP_JSON}" 2 -b 3 --json)"
echo "${LOAD_OUT2}" | grep -q '"name": "Second Profile"'
echo "${LOAD_OUT2}" | grep -q '"brightness": 3'

# 4.3 Nur -b Flag ohne Profil-Angabe: Erstes Profil mit Helligkeit 2
LOAD_OUT3="$("${CIY_CTL}" load "${TMP_JSON}" -b 2 --json)"
echo "${LOAD_OUT3}" | grep -q '"name": "First Profile"'
echo "${LOAD_OUT3}" | grep -q '"brightness": 2'

# 4.4 Legacy-Syntax: 2 positionale Argumente (Name + Helligkeit)
LOAD_OUT4="$("${CIY_CTL}" load "${TMP_JSON}" "Second Profile" 4 --json)"
echo "${LOAD_OUT4}" | grep -q '"name": "Second Profile"'
echo "${LOAD_OUT4}" | grep -q '"brightness": 4'

# 4.5 Ablehnung ungültiger Profile beim Laden (Validation Guard)
set +e
"${CIY_CTL}" load "${PROJECT_ROOT}/tests/fixtures/invalid_slot_range.json" 2>/dev/null
if [ $? -eq 0 ]; then
    echo "FEHLER: ciy_ctl load akzeptierte ungültiges Profil!"
    exit 1
fi

LOAD_BAD_JSON="$("${CIY_CTL}" load "${PROJECT_ROOT}/tests/fixtures/invalid_slot_range.json" --json)"
echo "${LOAD_BAD_JSON}" | grep -q '"success": false'
echo "${LOAD_BAD_JSON}" | grep -q '"field": "130"'
set -e

echo "=== Test CLI: 5. Status-/Set-Exit-Codes und JSON-Schema ==="
STATUS_OK="$("${CIY_CTL}" status --json)"
echo "${STATUS_OK}" | grep -q '"connected": true'

STATUS_FAIL_OUT="${TMP_DIR}/status_fail.json"
set +e
CIY_TESTING_FAIL_GET_REPORTS=1 "${CIY_CTL}" status --json > "${STATUS_FAIL_OUT}"
STATUS_FAIL_RC=$?
set -e
if [ ${STATUS_FAIL_RC} -eq 0 ]; then
    echo "FEHLER: status meldet bei Lesefehler Exit-Code 0!"
    exit 1
fi
grep -q '"connected": false' "${STATUS_FAIL_OUT}"

SET_OK="$("${CIY_CTL}" set 15 --json)"
echo "${SET_OK}" | grep -q '"success": true'
echo "${SET_OK}" | grep -q '"action": "set"'
echo "${SET_OK}" | grep -q '"mode": 15'

SET_FAIL_OUT="${TMP_DIR}/set_fail.json"
set +e
CIY_TESTING_FAIL_GET_REPORTS=1 "${CIY_CTL}" set 15 --json > "${SET_FAIL_OUT}"
SET_FAIL_RC=$?
set -e
if [ ${SET_FAIL_RC} -eq 0 ]; then
    echo "FEHLER: set meldet bei Lesefehler Exit-Code 0!"
    exit 1
fi
grep -q '"success": false' "${SET_FAIL_OUT}"

echo "=== CLI-Tests erfolgreich bestanden! ==="
