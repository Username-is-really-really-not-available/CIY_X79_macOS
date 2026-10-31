#!/bin/bash
set -e
export CIY_TESTING=1
export CIY_SANITIZE=1
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${PROJECT_ROOT}"

SAN_FLAGS=(-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)

trap 'rm -f /tmp/test_ciy_protocol /tmp/test_ciy_hid /tmp/test_profile_logic /tmp/test_openrgb /tmp/test_ciy_hidapi /tmp/ciy_hid_hidapi.o /tmp/ciy_x79_iso_de.o /tmp/test_ciy_battery /tmp/test_ciy_gradient' EXIT

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

echo "=== Test 0b: OpenRGB Contribution Bundle Tests (ASan/UBSan, C++17) ==="
clang++ -Wall -Wextra -Werror -std=c++17 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -I tests/openrgb_mock -I contrib/openrgb \
    tests/openrgb_mock/hidapi_mock.cpp \
    contrib/openrgb/CIYController.cpp \
    contrib/openrgb/RGBController_CIYX79.cpp \
    contrib/openrgb/CIYX79Detector.cpp \
    tests/test_openrgb.cpp \
    -o /tmp/test_openrgb
/tmp/test_openrgb

echo "=== Test 0c: Battery & Gradient Engine Tests (ASan/UBSan, C11) ==="
clang -Wall -Wextra -Werror -std=c11 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -framework IOKit \
    -framework CoreFoundation \
    tests/test_ciy_battery.c \
    -o /tmp/test_ciy_battery
/tmp/test_ciy_battery

clang -Wall -Wextra -Werror -std=c11 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    src/layouts/ciy_x79_iso_de.c \
    tests/test_ciy_gradient.c \
    -o /tmp/test_ciy_gradient
/tmp/test_ciy_gradient

echo "=== Test 1: HID & Protokoll Tests (macOS IOKit, ASan/UBSan) ==="
clang -Wall -Wextra -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -framework IOKit \
    -framework CoreFoundation \
    src/core/ciy_hid.c \
    src/layouts/ciy_x79_iso_de.c \
    tests/test_ciy_hid.c \
    -o /tmp/test_ciy_hid
/tmp/test_ciy_hid

echo "=== Test 1b: Linux hidapi Backend Tests (ASan/UBSan, C11 & C++) ==="
clang -Wall -Wextra -Werror -std=c11 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -DCIY_BACKEND_HIDAPI -I tests/openrgb_mock -I src/core \
    -c src/core/ciy_hid.c -o /tmp/ciy_hid_hidapi.o
clang++ -Wall -Wextra -Werror -std=c++17 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -I tests/openrgb_mock -I src/core \
    /tmp/ciy_hid_hidapi.o \
    tests/openrgb_mock/hidapi_mock.cpp \
    tests/test_ciy_hidapi.cpp \
    -o /tmp/test_ciy_hidapi
/tmp/test_ciy_hidapi

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
    src/app/CIYBatteryMonitor.m \
    tests/test_profile_logic.m \
    -o /tmp/test_profile_logic
/tmp/test_profile_logic

echo "=== Test 3: CLI ciy_ctl Tests (frisch kompiliert) ==="
./tests/test_ciy_ctl.sh

echo "=== Alle Test-Suiten erfolgreich durchgelaufen! ==="
