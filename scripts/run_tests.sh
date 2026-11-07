#!/bin/bash
set -e
export CIY_TESTING=1
export CIY_SANITIZE=1
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${PROJECT_ROOT}"

TEST_TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ciy_tests.XXXXXX")"
TEST_LOCK_DIR="${TMPDIR:-/tmp}/ciy_x79_test_suite_$(id -u).lock"
TEST_LOCK_OWNED=0
cleanup() {
    rm -rf "${TEST_TMP_DIR}" || true
    if [ "${TEST_LOCK_OWNED}" -eq 1 ]; then
        rmdir "${TEST_LOCK_DIR}" 2>/dev/null || true
    fi
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

if command -v pgrep >/dev/null 2>&1; then
    if pgrep -x CIYX79Control >/dev/null 2>&1 || pgrep -f 'CIYX79Control.app/Contents/MacOS/CIYX79Control' >/dev/null 2>&1; then
        echo "FEHLER: CIYX79Control läuft. Bitte zuerst beenden; der hidapi-Test nutzt den produktiven Bus-Lock." >&2
        exit 1
    fi
fi

if ! mkdir "${TEST_LOCK_DIR}" 2>/dev/null; then
    echo "FEHLER: Eine andere Testsuite läuft bereits." >&2
    echo "Lock: ${TEST_LOCK_DIR}" >&2
    echo "Wenn sicher kein Test läuft: rm -rf '${TEST_LOCK_DIR}'" >&2
    exit 1
fi
TEST_LOCK_OWNED=1

SAN_FLAGS=(-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
HOOK_FLAGS=(-DCIY_ENABLE_TEST_HOOKS=1)

echo "=== Test 0: Standalone Protocol & Zero-OS-Lock-in Tests (ASan/UBSan, C11 & C++) ==="
clang -Wall -Wextra -Werror -std=c11 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    src/layouts/ciy_x79_iso_de.c \
    tests/test_ciy_protocol.c \
    -o "${TEST_TMP_DIR}/test_ciy_protocol"
"${TEST_TMP_DIR}/test_ciy_protocol"

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
    -o "${TEST_TMP_DIR}/test_openrgb"
"${TEST_TMP_DIR}/test_openrgb"

echo "=== Test 0c: Battery & Gradient Engine Tests (ASan/UBSan, C11) ==="
clang -Wall -Wextra -Werror -std=c11 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -framework IOKit \
    -framework CoreFoundation \
    "${HOOK_FLAGS[@]}" \
    tests/test_ciy_battery.c \
    -o "${TEST_TMP_DIR}/test_ciy_battery"
"${TEST_TMP_DIR}/test_ciy_battery"

clang -Wall -Wextra -Werror -std=c11 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    src/layouts/ciy_x79_iso_de.c \
    tests/test_ciy_gradient.c \
    -o "${TEST_TMP_DIR}/test_ciy_gradient"
"${TEST_TMP_DIR}/test_ciy_gradient"

echo "=== Test 0d: Per-Key Brightness Engine (ASan/UBSan, C11 & C++, CLI) ==="
clang -Wall -Wextra -Werror -std=c11 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    src/layouts/ciy_x79_iso_de.c \
    tests/test_ciy_brightness.c \
    -o "${TEST_TMP_DIR}/test_ciy_brightness"

clang++ -Wall -Wextra -Werror -std=c++17 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -x c++ -c src/core/ciy_brightness.h -o /dev/null

if grep -nE '#include *[<"](IOKit|Cocoa|CoreFoundation|Foundation|AppKit)' src/core/ciy_brightness.h; then
    echo "FEHLER: ciy_brightness.h enthält OS-Framework-Includes!" >&2
    exit 1
fi

clang -Wall -Wextra -Werror -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -framework IOKit \
    -framework CoreFoundation \
    "${HOOK_FLAGS[@]}" \
    src/core/ciy_hid.c \
    src/layouts/ciy_x79_iso_de.c \
    src/cli/ciy_ctl.c \
    -o "${TEST_TMP_DIR}/ciy_ctl_brightness_test"

"${TEST_TMP_DIR}/test_ciy_brightness" "${TEST_TMP_DIR}/ciy_ctl_brightness_test"

echo "=== Test 1: HID & Protokoll Tests (macOS IOKit, ASan/UBSan) ==="
clang -Wall -Wextra -Werror -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -framework IOKit \
    -framework CoreFoundation \
    "${HOOK_FLAGS[@]}" \
    src/core/ciy_hid.c \
    src/layouts/ciy_x79_iso_de.c \
    tests/test_ciy_hid.c \
    -o "${TEST_TMP_DIR}/test_ciy_hid"
"${TEST_TMP_DIR}/test_ciy_hid"

echo "=== Test 1b: Linux hidapi Backend Tests (ASan/UBSan, C11 & C++) ==="
clang -Wall -Wextra -Werror -std=c11 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -DCIY_BACKEND_HIDAPI -I tests/openrgb_mock -I src/core \
    "${HOOK_FLAGS[@]}" \
    -c src/core/ciy_hid.c -o "${TEST_TMP_DIR}/ciy_hid_hidapi.o"
clang++ -Wall -Wextra -Werror -std=c++17 -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -I tests/openrgb_mock -I src/core \
    "${TEST_TMP_DIR}/ciy_hid_hidapi.o" \
    tests/openrgb_mock/hidapi_mock.cpp \
    tests/test_ciy_hidapi.cpp \
    -o "${TEST_TMP_DIR}/test_ciy_hidapi"
"${TEST_TMP_DIR}/test_ciy_hidapi"

echo "=== Test 2: Profil & KeyboardView Tests (ASan/UBSan) ==="
clang -Wall -Wextra -Werror -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -fobjc-arc \
    -framework Cocoa \
    -framework IOKit \
    -framework CoreFoundation \
    "${HOOK_FLAGS[@]}" \
    src/core/ciy_hid.c \
    src/layouts/ciy_x79_iso_de.c \
    src/app/KeyboardBackend.m \
    src/app/CIYKeyboardView.m \
    src/app/CIYBatteryMonitor.m \
    tests/test_profile_logic.m \
    -o "${TEST_TMP_DIR}/test_profile_logic"
"${TEST_TMP_DIR}/test_profile_logic"

echo "=== Test 2b: Localization Tests (ASan/UBSan) ==="
# de.lproj neben das Test-Binary legen, damit der produktive
# [NSBundle mainBundle]-Pfad ohne Test-Hook aufgelöst werden kann.
cp -R resources/de.lproj "${TEST_TMP_DIR}/"
clang -Wall -Wextra -Werror -mmacosx-version-min=12.0 "${SAN_FLAGS[@]}" \
    -fobjc-arc \
    -framework Foundation \
    "${HOOK_FLAGS[@]}" \
    src/app/CIYLocalization.m \
    tests/test_ciy_localization.m \
    -o "${TEST_TMP_DIR}/test_ciy_localization"
"${TEST_TMP_DIR}/test_ciy_localization"

echo "=== Test 2c: Localization Resource Lint ==="
plutil -lint resources/en.lproj/Localizable.strings
plutil -lint resources/de.lproj/Localizable.strings

echo "=== Test 2d: Release-Hygiene (Test-Hooks auskompiliert) ==="
clang -Wall -Wextra -Werror -std=c11 -mmacosx-version-min=12.0 -O2 \
    -UCIY_ENABLE_TEST_HOOKS \
    -framework IOKit -framework CoreFoundation \
    src/core/ciy_hid.c src/layouts/ciy_x79_iso_de.c src/cli/ciy_ctl.c \
    -o "${TEST_TMP_DIR}/ciy_ctl_release"
if strings "${TEST_TMP_DIR}/ciy_ctl_release" | grep -qE 'CIY_TESTING|CIY_FAIL_ON_HARDWARE_IO|CIY_BATTERY_MOCK'; then
    echo "FEHLER: Release-Binary enthält Test-Hook-Strings!" >&2
    exit 1
fi

echo "=== Test 3: CLI ciy_ctl Tests (frisch kompiliert) ==="
./tests/test_ciy_ctl.sh

echo "=== Alle Test-Suiten erfolgreich durchgelaufen! ==="
