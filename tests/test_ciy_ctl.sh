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
if "${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/invalid_slot_range.json" 2>/dev/null; then
    echo "FEHLER: validate-profile akzeptierte ungültigen Slot!"
    exit 1
fi

BAD_SLOT_JSON="$("${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/invalid_slot_range.json" --json 2>/dev/null)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: validate-profile --json mit invalid_slot_range.json hätte fehlschlagen müssen!"
    exit 1
fi
echo "${BAD_SLOT_JSON}" | grep -q '"valid": false'
echo "${BAD_SLOT_JSON}" | grep -q '"field": "130"'

if "${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/invalid_color.json" 2>/dev/null; then
    echo "FEHLER: validate-profile akzeptierte ungültige Farbe!"
    exit 1
fi

BAD_COLOR_JSON="$("${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/invalid_color.json" --json 2>/dev/null)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: validate-profile --json mit invalid_color.json hätte fehlschlagen müssen!"
    exit 1
fi
echo "${BAD_COLOR_JSON}" | grep -q '"valid": false'
echo "${BAD_COLOR_JSON}" | grep -q '"field": "14"'

if "${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/invalid_syntax.json" 2>/dev/null; then
    echo "FEHLER: validate-profile akzeptierte korruptes JSON!"
    exit 1
fi

BAD_SYNTAX_JSON="$("${CIY_CTL}" validate-profile "${PROJECT_ROOT}/tests/fixtures/invalid_syntax.json" --json 2>/dev/null)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: validate-profile --json mit invalid_syntax.json hätte fehlschlagen müssen!"
    exit 1
fi
echo "${BAD_SYNTAX_JSON}" | grep -q '"valid": false'

# Fehlendes Argument
if "${CIY_CTL}" validate-profile 2>/dev/null; then
    echo "FEHLER: validate-profile ohne Argument akzeptiert!"
    exit 1
fi

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
if "${CIY_CTL}" --dry-run load "${PROJECT_ROOT}/tests/fixtures/invalid_slot_range.json" 2>/dev/null; then
    echo "FEHLER: --dry-run load akzeptierte ungültiges Profil!"
    exit 1
fi

DRY_BAD_JSON="$("${CIY_CTL}" --dry-run load "${PROJECT_ROOT}/tests/fixtures/invalid_slot_range.json" --json 2>/dev/null)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: --dry-run load mit invalid_slot_range.json hätte fehlschlagen müssen!"
    exit 1
fi
echo "${DRY_BAD_JSON}" | grep -q '"success": false'
echo "${DRY_BAD_JSON}" | grep -q '"field": "130"'

# 5. --dump-bytes Inspektion (Text & JSON)
DRY_HEX="$("${CIY_CTL}" --dry-run load "${PROJECT_ROOT}/tests/fixtures/valid_profile.json" --dump-bytes)"
echo "${DRY_HEX}" | grep -q "Raw Wire Bytes (Hex):"
echo "${DRY_HEX}" | grep -q "Report 0x0B"
DRY_HEX_JSON="$("${CIY_CTL}" --dry-run load "${PROJECT_ROOT}/tests/fixtures/valid_profile.json" --dump-bytes --json)"
echo "${DRY_HEX_JSON}" | grep -q '"hex": "0b'

# 6. Universeller --dry-run für Schreibbefehle (fill, set, wasd, wasd-arrows, all-red)
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

DRY_WASD_B="$("${CIY_CTL}" --dry-run wasd -b 2 --json)"
echo "${DRY_WASD_B}" | grep -q '"brightness": 2'

DRY_WASD_ARROWS_B="$("${CIY_CTL}" --dry-run wasd-arrows -b 2 --json)"
echo "${DRY_WASD_ARROWS_B}" | grep -q '"brightness": 2'

DRY_ALL_RED_B="$("${CIY_CTL}" --dry-run all-red -b 4 --json)"
echo "${DRY_ALL_RED_B}" | grep -q '"brightness": 4'

# 7. --dry-run mit nicht-unterstütztem Befehl (z. B. validate-profile) muss fehlschlagen
DRY_VAL_ERR="$("${CIY_CTL}" --dry-run validate-profile "${PROJECT_ROOT}/tests/fixtures/valid_profile.json" 2>&1)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: --dry-run validate-profile hätte fehlschlagen müssen!"
    exit 1
fi
echo "${DRY_VAL_ERR}" | grep -q "nur für Schreibbefehle"

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
if "${CIY_CTL}" --unknown-option 2>/dev/null; then
    echo "FEHLER: Unbekannte Option wurde akzeptiert!"
    exit 1
fi

ERR_JSON="$("${CIY_CTL}" --unknown-option --json 2>/dev/null)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: --unknown-option --json hätte fehlschlagen müssen!"
    exit 1
fi
echo "${ERR_JSON}" | grep -q "Unbekannte Option"

if "${CIY_CTL}" -b 9 status 2>/dev/null; then
    echo "FEHLER: Ungültige Helligkeit 9 wurde akzeptiert!"
    exit 1
fi

BRIGHT_ERR_JSON="$("${CIY_CTL}" -b 9 --json status 2>/dev/null)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: -b 9 --json status hätte fehlschlagen müssen!"
    exit 1
fi
echo "${BRIGHT_ERR_JSON}" | grep -q "Ungültige Helligkeit"

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
if "${CIY_CTL}" load "${PROJECT_ROOT}/tests/fixtures/invalid_slot_range.json" 2>/dev/null; then
    echo "FEHLER: ciy_ctl load akzeptierte ungültiges Profil!"
    exit 1
fi

LOAD_BAD_JSON="$("${CIY_CTL}" load "${PROJECT_ROOT}/tests/fixtures/invalid_slot_range.json" --json 2>/dev/null)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: load invalid_slot_range.json --json hätte fehlschlagen müssen!"
    exit 1
fi
echo "${LOAD_BAD_JSON}" | grep -q '"success": false'
echo "${LOAD_BAD_JSON}" | grep -q '"field": "130"'

echo "=== Test CLI: 5. Status-/Set-Exit-Codes und JSON-Schema ==="
STATUS_OK="$("${CIY_CTL}" status --json)"
echo "${STATUS_OK}" | grep -q '"connected": true'

STATUS_FAIL_OUT="${TMP_DIR}/status_fail.json"
if CIY_TESTING_FAIL_GET_REPORTS=1 "${CIY_CTL}" status --json > "${STATUS_FAIL_OUT}"; then
    echo "FEHLER: status meldet bei Lesefehler Exit-Code 0!"
    exit 1
fi
grep -q '"connected": false' "${STATUS_FAIL_OUT}"

SET_OK="$("${CIY_CTL}" set 15 --json)"
echo "${SET_OK}" | grep -q '"success": true'
echo "${SET_OK}" | grep -q '"action": "set"'
echo "${SET_OK}" | grep -q '"mode": 15'

SET_B_OK="$("${CIY_CTL}" set 15 -b 3 --json)"
echo "${SET_B_OK}" | grep -q '"success": true'
echo "${SET_B_OK}" | grep -q '"mode": 15'
echo "${SET_B_OK}" | grep -q '"brightness": 3'

WASD_B_OK="$("${CIY_CTL}" wasd -b 1 --json)"
echo "${WASD_B_OK}" | grep -q '"success": true'
echo "${WASD_B_OK}" | grep -q '"action": "wasd"'
echo "${WASD_B_OK}" | grep -q '"brightness": 1'

WASD_ARROWS_B_OK="$("${CIY_CTL}" wasd-arrows -b 3 --json)"
echo "${WASD_ARROWS_B_OK}" | grep -q '"success": true'
echo "${WASD_ARROWS_B_OK}" | grep -q '"action": "wasd-arrows"'
echo "${WASD_ARROWS_B_OK}" | grep -q '"brightness": 3'

ALL_RED_B_OK="$("${CIY_CTL}" all-red -b 2 --json)"
echo "${ALL_RED_B_OK}" | grep -q '"success": true'
echo "${ALL_RED_B_OK}" | grep -q '"action": "all-red"'
echo "${ALL_RED_B_OK}" | grep -q '"brightness": 2'

SET_FAIL_OUT="${TMP_DIR}/set_fail.json"
if CIY_TESTING_FAIL_GET_REPORTS=1 "${CIY_CTL}" set 15 --json > "${SET_FAIL_OUT}"; then
    echo "FEHLER: set meldet bei Lesefehler Exit-Code 0!"
    exit 1
fi
grep -q '"success": false' "${SET_FAIL_OUT}"

# Wenn alle 4 Parameter angegeben sind (Modus, Farbe, Tempo, Helligkeit via -b oder positional),
# darf kein unnötiger Status-Lesezugriff erfolgen und der Befehl muss trotz Lesefehler erfolgreich sein:
SET_NO_READ_B_OK="$(CIY_TESTING_FAIL_GET_REPORTS=1 "${CIY_CTL}" set 15 0 2 -b 3 --json)"
echo "${SET_NO_READ_B_OK}" | grep -q '"success": true'
echo "${SET_NO_READ_B_OK}" | grep -q '"mode": 15'
echo "${SET_NO_READ_B_OK}" | grep -q '"brightness": 3'

SET_NO_READ_POS_OK="$(CIY_TESTING_FAIL_GET_REPORTS=1 "${CIY_CTL}" set 15 0 2 3 --json)"
echo "${SET_NO_READ_POS_OK}" | grep -q '"success": true'
echo "${SET_NO_READ_POS_OK}" | grep -q '"brightness": 3'

echo "=== Test CLI: 6. probe-led und sweep (Phase 2 Probing & Diagnostics) ==="
# 6.1 probe-led Erfolgspfade
PROBE_ESC="$("${CIY_CTL}" probe-led 2 255 0 0 --json)"
echo "${PROBE_ESC}" | grep -q '"success": true'
echo "${PROBE_ESC}" | grep -q '"action": "probe-led"'
echo "${PROBE_ESC}" | grep -q '"led_index": 2'
echo "${PROBE_ESC}" | grep -q '"matrix_slot": 1'
echo "${PROBE_ESC}" | grep -q '"key_name": "Esc"'
echo "${PROBE_ESC}" | grep -q '"color": "#FF0000"'

PROBE_SLOT="$("${CIY_CTL}" probe-led --slot 14 0 255 0 --json)"
echo "${PROBE_SLOT}" | grep -q '"success": true'
echo "${PROBE_SLOT}" | grep -q '"led_index": 15'
echo "${PROBE_SLOT}" | grep -q '"matrix_slot": 14'
echo "${PROBE_SLOT}" | grep -q '"key_name": "W"'
echo "${PROBE_SLOT}" | grep -q '"color": "#00FF00"'

PROBE_HEX="$("${CIY_CTL}" probe-led 15 "#0000FF" -b 3 --json)"
echo "${PROBE_HEX}" | grep -q '"success": true'
echo "${PROBE_HEX}" | grep -q '"brightness": 3'
echo "${PROBE_HEX}" | grep -q '"color": "#0000FF"'

# 6.2 probe-led Grenzwerte & Fehlerbehandlung
if "${CIY_CTL}" probe-led 126 2>/dev/null; then
    echo "FEHLER: probe-led 126 hätte fehlschlagen müssen!"
    exit 1
fi
if "${CIY_CTL}" probe-led -1 2>/dev/null; then
    echo "FEHLER: probe-led -1 hätte fehlschlagen müssen!"
    exit 1
fi
if "${CIY_CTL}" probe-led --slot 125 2>/dev/null; then
    echo "FEHLER: probe-led --slot 125 hätte fehlschlagen müssen!"
    exit 1
fi
if "${CIY_CTL}" probe-led --slot -1 2>/dev/null; then
    echo "FEHLER: probe-led --slot -1 hätte fehlschlagen müssen!"
    exit 1
fi

PROBE_BAD_JSON="$("${CIY_CTL}" probe-led 126 --json 2>/dev/null)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: probe-led 126 --json hätte fehlschlagen müssen!"
    exit 1
fi
echo "${PROBE_BAD_JSON}" | grep -q '"success": false'
echo "${PROBE_BAD_JSON}" | grep -q "Erlaubter Bereich: 0..125"

# 6.3 probe-led Dry-Run & Wire-Bytes
PROBE_DRY="$("${CIY_CTL}" --dry-run --dump-bytes probe-led 2 255 0 0)"
echo "${PROBE_DRY}" | grep -q "DRY RUN"
echo "${PROBE_DRY}" | grep -q "Report 0x0B"
echo "${PROBE_DRY}" | grep -q "Report 0x0A"
echo "${PROBE_DRY}" | grep -q "Raw Wire Bytes"

# 6.3b probe-led Ablehnung überzähliger Argumente
PROBE_EXTRA_ERR="$("${CIY_CTL}" --dry-run probe-led 2 255 0 0 99 2>&1)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: probe-led mit überzähligen Argumenten hätte fehlschlagen müssen!"
    exit 1
fi
echo "${PROBE_EXTRA_ERR}" | grep -q "Zu viele Argumente"

# 6.4 sweep Nicht-interaktiv ohne --auto muss fehlschlagen
SWEEP_NO_TTY="$("${CIY_CTL}" sweep --json < /dev/null 2>/dev/null)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: sweep ohne TTY und ohne --auto hätte fehlschlagen müssen!"
    exit 1
fi
echo "${SWEEP_NO_TTY}" | grep -q '"success": false'
echo "${SWEEP_NO_TTY}" | grep -q "erfordert ein interaktives Terminal"

# 6.4b sweep Ablehnung unerwarteter Positionsargumente
SWEEP_EXTRA_ERR="$("${CIY_CTL}" sweep extra_arg 2>&1)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: sweep mit Positionsargumenten hätte fehlschlagen müssen!"
    exit 1
fi
echo "${SWEEP_EXTRA_ERR}" | grep -q "Unerwartetes Argument"

# 6.5 sweep Mindestverzögerung < 100 ms muss abgelehnt werden
SWEEP_BAD_DELAY="$("${CIY_CTL}" sweep --auto --delay-ms 50 --json 2>/dev/null)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: sweep --delay-ms 50 hätte fehlschlagen müssen!"
    exit 1
fi
echo "${SWEEP_BAD_DELAY}" | grep -q '"success": false'
echo "${SWEEP_BAD_DELAY}" | grep -q "Mindestwert: 100 ms"

# 6.6 sweep Dry-Run & Wire-Bytes
SWEEP_DRY="$("${CIY_CTL}" --dry-run --dump-bytes sweep)"
echo "${SWEEP_DRY}" | grep -q "DRY RUN"
echo "${SWEEP_DRY}" | grep -q "Sweep Simulation"
echo "${SWEEP_DRY}" | grep -q "Report 0x0B"

# 6.7 sweep interaktive Flags (-i / --interactive) im Dry-Run
"${CIY_CTL}" --dry-run sweep -i > /dev/null
"${CIY_CTL}" --dry-run sweep --interactive > /dev/null

echo "=== Test CLI: 7. battery Subcommand (Mocks, Quellen, Dry-Run & JSON) ==="
# 7.1 Hilfe enthält battery
"${CIY_CTL}" --help | grep -q "battery"
"${CIY_CTL}" --help | grep -q -- "--target"
"${CIY_CTL}" --help | grep -q -- "--list-sources"

# 7.2 list-sources (Text & JSON)
BAT_SRC_TXT="$("${CIY_CTL}" battery --list-sources)"
echo "${BAT_SRC_TXT}" | grep -q "Verfügbare Stromquellen"
BAT_SRC_JSON="$("${CIY_CTL}" battery --list-sources --json)"
echo "${BAT_SRC_JSON}" | grep -q '"success": true'
echo "${BAT_SRC_JSON}" | grep -q '"sources":'

# 7.3 battery Info-Modus mit Mock (Text & JSON)
BAT_MOCK_TXT="$(CIY_BATTERY_MOCK_PERCENT=85 CIY_BATTERY_MOCK_CHARGING=0 "${CIY_CTL}" battery)"
echo "${BAT_MOCK_TXT}" | grep -q "Ladestand:    85%"
echo "${BAT_MOCK_TXT}" | grep -q "00E676"

BAT_MOCK_JSON="$(CIY_BATTERY_MOCK_PERCENT=15 CIY_BATTERY_MOCK_CHARGING=0 "${CIY_CTL}" battery --json)"
echo "${BAT_MOCK_JSON}" | grep -q '"percent": 15'
echo "${BAT_MOCK_JSON}" | grep -q '"color": "#FF1744"'
echo "${BAT_MOCK_JSON}" | grep -q '"target": "1-0"'

BAT_MOCK_UNAVAIL="$(CIY_BATTERY_MOCK_PERCENT=-1 CIY_BATTERY_MOCK_CHARGING=0 "${CIY_CTL}" battery --json)"
echo "${BAT_MOCK_UNAVAIL}" | grep -q '"percent": -1'

# 7.4 battery Dry-Run (Planar Report 0x0B & Report 0x0A Mode 19)
BAT_DRY="$(CIY_BATTERY_MOCK_PERCENT=70 CIY_BATTERY_MOCK_CHARGING=1 "${CIY_CTL}" battery --dry-run)"
echo "${BAT_DRY}" | grep -q "DRY RUN"
echo "${BAT_DRY}" | grep -q "Akku-Status (70%, #00E5FF)"
echo "${BAT_DRY}" | grep -q "Report 0x0B"
echo "${BAT_DRY}" | grep -q "Report 0x0A"

BAT_DRY_JSON="$(CIY_BATTERY_MOCK_PERCENT=40 CIY_BATTERY_MOCK_CHARGING=0 "${CIY_CTL}" battery --dry-run --json)"
echo "${BAT_DRY_JSON}" | grep -q '"dry_run": true'
echo "${BAT_DRY_JSON}" | grep -q '"mode": 19'

BAT_DUMP="$(CIY_BATTERY_MOCK_PERCENT=50 CIY_BATTERY_MOCK_CHARGING=0 "${CIY_CTL}" battery --dry-run --dump-bytes)"
echo "${BAT_DUMP}" | grep -q "Raw Wire Bytes"

# 7.5 battery Targets (space, nav, custom)
BAT_SPACE="$(CIY_BATTERY_MOCK_PERCENT=100 CIY_BATTERY_MOCK_CHARGING=0 "${CIY_CTL}" battery --target space --json)"
echo "${BAT_SPACE}" | grep -q '"target": "space"'
echo "${BAT_SPACE}" | grep -q '"target_slots": \[35\]'

# 7.5b Space bei 0% muss Warn-Rot (#FF1744) und 1 aktive Taste melden
BAT_SPACE_ZERO="$(CIY_BATTERY_MOCK_PERCENT=0 CIY_BATTERY_MOCK_CHARGING=0 "${CIY_CTL}" battery --target space --json)"
echo "${BAT_SPACE_ZERO}" | grep -q '"percent": 0'
echo "${BAT_SPACE_ZERO}" | grep -q '"color": "#FF1744"'
echo "${BAT_SPACE_ZERO}" | grep -q '"active_keys": 1'

BAT_NAV="$(CIY_BATTERY_MOCK_PERCENT=50 CIY_BATTERY_MOCK_CHARGING=0 "${CIY_CTL}" battery --target nav --json)"
echo "${BAT_NAV}" | grep -q '"target": "nav"'
echo "${BAT_NAV}" | grep -q '91, 94, 87, 86'

BAT_CUSTOM="$(CIY_BATTERY_MOCK_PERCENT=50 CIY_BATTERY_MOCK_CHARGING=0 "${CIY_CTL}" battery --target 14,9,15,21 --json)"
echo "${BAT_CUSTOM}" | grep -q '14, 9, 15, 21'

# 7.6 Ungültiges Akku-Ziel muss fehlschlagen
if "${CIY_CTL}" battery --target "invalid_slot" 2>/dev/null; then
    echo "FEHLER: battery mit ungültigem Ziel hätte fehlschlagen müssen!"
    exit 1
fi
BAT_BAD_TARGET="$("${CIY_CTL}" battery --target "invalid_slot" --json 2>/dev/null)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: battery --target invalid_slot --json hätte fehlschlagen müssen!"
    exit 1
fi
echo "${BAT_BAD_TARGET}" | grep -q '"success": false'
echo "${BAT_BAD_TARGET}" | grep -q "Ungültiges Akku-Ziel"

echo "=== Test CLI: 8. gradient Subcommand (Optionen, Richtungen, Dry-Run & JSON) ==="
# 8.1 Hilfe enthält gradient und --dir
"${CIY_CTL}" --help | grep -q "gradient"
"${CIY_CTL}" --help | grep -q -- "--dir"

# 8.2 gradient Dry-Run (Horizontal & Vertikal)
GRAD_DRY_H="$("${CIY_CTL}" --dry-run gradient '#00FFFF' '#FF00FF')"
echo "${GRAD_DRY_H}" | grep -q "DRY RUN"
echo "${GRAD_DRY_H}" | grep -q "Verlauf (#00FFFF -> #FF00FF, horizontal)"
echo "${GRAD_DRY_H}" | grep -q "Report 0x0B"
echo "${GRAD_DRY_H}" | grep -q "Report 0x0A"

GRAD_DRY_V="$("${CIY_CTL}" --dry-run gradient '#FFCC00' '#0066FF' --dir vertical)"
echo "${GRAD_DRY_V}" | grep -q "Verlauf (#FFCC00 -> #0066FF, vertikal)"

# 8.3 gradient Dry-Run JSON & Dump Bytes
GRAD_DRY_JSON="$("${CIY_CTL}" --dry-run gradient '#112233' '#445566' --json)"
echo "${GRAD_DRY_JSON}" | grep -q '"dry_run": true'
echo "${GRAD_DRY_JSON}" | grep -q '"mode": 19'

GRAD_DUMP="$("${CIY_CTL}" --dry-run gradient '#112233' '#445566' --dump-bytes)"
echo "${GRAD_DUMP}" | grep -q "Raw Wire Bytes"

# 8.4 Fehlerbehandlung fehlende oder ungültige Farben
if "${CIY_CTL}" gradient '#00FFFF' 2>/dev/null; then
    echo "FEHLER: gradient mit nur einer Farbe hätte fehlschlagen müssen!"
    exit 1
fi

if "${CIY_CTL}" gradient '#GGGGGG' '#000000' 2>/dev/null; then
    echo "FEHLER: gradient mit ungültiger Hex-Farbe hätte fehlschlagen müssen!"
    exit 1
fi

GRAD_ERR_JSON="$("${CIY_CTL}" gradient '#00FFFF' --json 2>/dev/null)" && RC=0 || RC=$?
if [ "${RC}" -eq 0 ]; then
    echo "FEHLER: gradient #00FFFF --json hätte fehlschlagen müssen!"
    exit 1
fi
echo "${GRAD_ERR_JSON}" | grep -q "Fehlende Hex-Farben"

echo "=== CLI-Tests erfolgreich bestanden! ==="
