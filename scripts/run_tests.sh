#!/bin/bash
set -e
export CIY_TESTING=1
export CIY_SANITIZE=1
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${PROJECT_ROOT}"

SAN_FLAGS=(-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)

trap 'rm -f /tmp/test_ciy_protocol /tmp/test_ciy_hid /tmp/test_profile_logic' EXIT

echo "=== Test 0: Standalone Protocol & Zero-OS-Lock-in Tests (ASan/UBSan, C11 & C++) ==="
clang -Wall -Wextra -Werror -std=c11 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    src/layouts/ciy_x79_iso_de.c \
    tests/test_ciy_protocol.c \
    -o /tmp/test_ciy_protocol
/tmp/test_ciy_protocol

clang++ -Wall -Wextra -Werror -std=c++17 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -x c++ -c src/core/ciy_protocol.h -o /dev/null
clang++ -Wall -Wextra -Werror -std=c++17 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -x c++ -c src/layouts/ciy_layout.h -o /dev/null

echo "=== Test 1: HID & Protokoll Tests (ASan/UBSan) ==="
clang -Wall -Wextra -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -framework IOKit \
    -framework CoreFoundation \
    src/core/ciy_hid.c \
    src/layouts/ciy_x79_iso_de.c \
    tests/test_ciy_hid.c \
    -o /tmp/test_ciy_hid
/tmp/test_ciy_hid

echo "=== Test 2: Profil & KeyboardView Tests (ASan/UBSan) ==="
clang -Wall -Wextra -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -fobjc-arc \
    -framework Cocoa \
    -framework IOKit \
    -framework CoreFoundation \
    src/core/ciy_hid.c \
    src/layouts/ciy_x79_iso_de.c \
    src/app/KeyboardBackend.m \
    src/app/CIYKeyboardView.m \
    tests/test_profile_logic.m \
    -o /tmp/test_profile_logic
/tmp/test_profile_logic

echo "=== Test 3: CLI ciy_ctl Tests (frisch kompiliert) ==="
./tests/test_ciy_ctl.sh

echo "=== Alle Test-Suiten erfolgreich durchgelaufen! ==="
