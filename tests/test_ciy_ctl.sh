#!/bin/bash
set -e

export CIY_TESTING="${CIY_TESTING:-1}"

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CIY_CTL="${PROJECT_ROOT}/dist/ciy_ctl"

if [ ! -x "${CIY_CTL}" ]; then
    echo "Kompiliere ciy_ctl für Test..."
    clang -Wall -Wextra -framework IOKit -framework CoreFoundation \
        "${PROJECT_ROOT}/src/core/ciy_hid.c" "${PROJECT_ROOT}/src/cli/ciy_ctl.c" \
        -o "${CIY_CTL}"
fi

echo "=== Test CLI: 1. Hilfe & Optionen ==="
HELP_OUT="$("${CIY_CTL}" --help)"
echo "${HELP_OUT}" | grep -q "Verwendung:"
echo "${HELP_OUT}" | grep -q -- "-b, --brightness"
echo "${HELP_OUT}" | grep -q -- "--json"

echo "=== Test CLI: 2. list-modes (Text & JSON) ==="
MODES_TXT="$("${CIY_CTL}" list-modes)"
echo "${MODES_TXT}" | grep -q "Wave"
MODES_JSON="$("${CIY_CTL}" list-modes --json)"
echo "${MODES_JSON}" | grep -q '"modes":'
echo "${MODES_JSON}" | grep -q '"colors":'

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
TMP_JSON="/tmp/ciy_ctl_test_profiles_$$.json"
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

rm -f "${TMP_JSON}"

echo "=== CLI-Tests erfolgreich bestanden! ==="
