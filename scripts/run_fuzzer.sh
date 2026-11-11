#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "${PROJECT_ROOT}"

CC="${CC:-clang}"
JOBS="${JOBS:-4}"
WORKERS="${WORKERS:-4}"

FUZZ_SRC="tests/fuzz/fuzz_json_parser.c"
PARSER_SRC="src/core/ciy_json.c"
FUZZ_BIN="tests/fuzz/fuzzer"
SEED_DIR="tests/fuzz_corpus"
WORK_CORPUS_DIR="tests/fuzz/corpus_work"
ARTIFACT_DIR="tests/fuzz/artifacts"

PLATFORM_FLAGS=()
if [ "$(uname -s)" = "Darwin" ]; then
    PLATFORM_FLAGS=(-mmacosx-version-min=12.0)
fi

if [ ! -d "${SEED_DIR}" ]; then
    echo "FEHLER: Seed-Verzeichnis '${SEED_DIR}' nicht gefunden." >&2
    exit 1
fi

mkdir -p "tests/fuzz"

# Prüfen, ob die aktive Clang-Toolchain LibFuzzer (-fsanitize=fuzzer) mitliefert
PROBE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ciy_fuzz_probe.XXXXXX")"
cleanup_probe() {
    rm -rf "${PROBE_DIR}" || true
}
trap cleanup_probe EXIT

cat > "${PROBE_DIR}/probe.c" <<'EOF'
#include <stddef.h>
#include <stdint.h>
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t s) {
    (void)d;
    (void)s;
    return 0;
}
EOF

HAS_LIBFUZZER=0
if "${CC}" "${PLATFORM_FLAGS[@]}" -fsanitize=fuzzer -std=c11 "${PROBE_DIR}/probe.c" -o "${PROBE_DIR}/probe" >/dev/null 2>&1; then
    HAS_LIBFUZZER=1
fi
rm -rf "${PROBE_DIR}"
trap - EXIT

if [ "${HAS_LIBFUZZER}" -eq 1 ]; then
    echo "=== Kompiliere LibFuzzer-Harness (ASan + UBSan + LibFuzzer, ISO C11) ==="
    mkdir -p "${WORK_CORPUS_DIR}" "${ARTIFACT_DIR}"

    "${CC}" "${PLATFORM_FLAGS[@]}" -O2 -g \
        -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
        -fno-omit-frame-pointer -Wall -Wextra -Werror -std=c11 \
        "${FUZZ_SRC}" "${PARSER_SRC}" -Isrc/core \
        -o "${FUZZ_BIN}"

    FUZZ_ARGS=()
    HAS_JOBS_ARG=0
    HAS_WORKERS_ARG=0
    HAS_MERGE_ARG=0
    HAS_ARTIFACT_ARG=0

    for arg in "$@"; do
        case "${arg}" in
            -jobs=*) HAS_JOBS_ARG=1 ;;
            -workers=*) HAS_WORKERS_ARG=1 ;;
            -merge=*) HAS_MERGE_ARG=1 ;;
            -artifact_prefix=*) HAS_ARTIFACT_ARG=1 ;;
        esac
    done

    if [ "${HAS_ARTIFACT_ARG}" -eq 0 ]; then
        FUZZ_ARGS+=("-artifact_prefix=${ARTIFACT_DIR}/")
    fi
    if [ "${HAS_MERGE_ARG}" -eq 0 ]; then
        if [ "${HAS_JOBS_ARG}" -eq 0 ]; then
            FUZZ_ARGS+=("-jobs=${JOBS}")
        fi
        if [ "${HAS_WORKERS_ARG}" -eq 0 ]; then
            FUZZ_ARGS+=("-workers=${WORKERS}")
        fi
    fi

    echo "=== Starte Multi-Core LibFuzzer (Work-Corpus: ${WORK_CORPUS_DIR}, Seeds: ${SEED_DIR}) ==="
    exec "./${FUZZ_BIN}" "${FUZZ_ARGS[@]}" "$@" "${WORK_CORPUS_DIR}" "${SEED_DIR}"
else
    echo "=== HINWEIS: '${CC}' besitzt keine LibFuzzer-Runtime (-fsanitize=fuzzer, z. B. Apple Clang). ==="
    echo "=== Kompiliere Standalone-ASan/UBSan-Corpus-Runner (-DCIY_FUZZ_STANDALONE=1) ==="

    "${CC}" "${PLATFORM_FLAGS[@]}" -O2 -g \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -fno-omit-frame-pointer -Wall -Wextra -Werror -std=c11 \
        -DCIY_FUZZ_STANDALONE=1 \
        "${FUZZ_SRC}" "${PARSER_SRC}" -Isrc/core \
        -o "${FUZZ_BIN}"

    shopt -s nullglob
    SEEDS=("${SEED_DIR}"/*)
    shopt -u nullglob

    if [ "${#SEEDS[@]}" -eq 0 ]; then
        echo "FEHLER: Keine Seed-Dateien in '${SEED_DIR}' gefunden." >&2
        exit 1
    fi

    "./${FUZZ_BIN}" "${SEEDS[@]}"
fi
