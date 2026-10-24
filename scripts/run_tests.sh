#!/bin/bash
set -e
export CIY_TESTING=1
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${PROJECT_ROOT}"

trap 'rm -f /tmp/test_ciy_hid /tmp/test_profile_logic' EXIT

echo "=== Test 1: HID & Protokoll Tests ==="
clang -Wall -Wextra \
    -framework IOKit \
    -framework CoreFoundation \
    src/core/ciy_hid.c \
    tests/test_ciy_hid.c \
    -o /tmp/test_ciy_hid
/tmp/test_ciy_hid

echo "=== Test 2: Profil & KeyboardView Tests ==="
clang -Wall -Wextra \
    -fobjc-arc \
    -framework Cocoa \
    -framework IOKit \
    -framework CoreFoundation \
    src/core/ciy_hid.c \
    src/app/KeyboardBackend.m \
    src/app/CIYKeyboardView.m \
    tests/test_profile_logic.m \
    -o /tmp/test_profile_logic
/tmp/test_profile_logic

echo "=== Test 3: CLI ciy_ctl Tests ==="
./tests/test_ciy_ctl.sh

echo "=== Alle Test-Suiten erfolgreich durchgelaufen! ==="
