#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_PATH="${REPO_PATH:-$(cd "${SCRIPT_DIR}/.." && pwd)}"
WORKTREE_PATH="${1:-$PWD}"

if [ ! -d "${WORKTREE_PATH}" ]; then
    echo "FEHLER: Worktree-Verzeichnis existiert nicht: ${WORKTREE_PATH}" >&2
    exit 1
fi

WORKTREE_PATH="$(cd "${WORKTREE_PATH}" && pwd)"
REPO_PATH="$(cd "${REPO_PATH}" && pwd)"

if [ "${WORKTREE_PATH}" = "${REPO_PATH}" ]; then
    echo "FEHLER: Ziel ist das Hauptrepo. Bitte einen Worktree-Pfad angeben." >&2
    exit 1
fi

if ! git -C "${WORKTREE_PATH}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "FEHLER: ${WORKTREE_PATH} ist kein Git-Worktree." >&2
    exit 1
fi

TARGET_ROOT="$(git -C "${WORKTREE_PATH}" rev-parse --show-toplevel)"
if [ "${TARGET_ROOT}" != "${WORKTREE_PATH}" ]; then
    echo "FEHLER: ${WORKTREE_PATH} ist nicht das Root-Verzeichnis des Worktrees." >&2
    exit 1
fi

TARGET_COMMON="$(git -C "${WORKTREE_PATH}" rev-parse --git-common-dir)"
SOURCE_COMMON="$(git -C "${REPO_PATH}" rev-parse --git-common-dir)"
if [[ "${TARGET_COMMON}" != /* ]]; then
    TARGET_COMMON="$(cd "${WORKTREE_PATH}" && cd "${TARGET_COMMON}" && pwd)"
fi
if [[ "${SOURCE_COMMON}" != /* ]]; then
    SOURCE_COMMON="$(cd "${REPO_PATH}" && cd "${SOURCE_COMMON}" && pwd)"
fi
if [ "${TARGET_COMMON}" != "${SOURCE_COMMON}" ]; then
    echo "FEHLER: Ziel-Worktree und Quellrepo gehören nicht zum selben Git-Repository." >&2
    exit 1
fi

for required in "AGENTS.md" ".kiloignore" "docs/agent/hardware-safety.md"; do
    if [ ! -e "${REPO_PATH}/${required}" ]; then
        echo "FEHLER: Pflichtdatei fehlt im Quellrepo: ${required}" >&2
        exit 1
    fi
done

copy_path() {
    local relative="$1"
    local source_path="${REPO_PATH}/${relative}"
    local target_path="${WORKTREE_PATH}/${relative}"

    if [ ! -e "${source_path}" ]; then
        return 0
    fi
    if [ -e "${target_path}" ] || [ -L "${target_path}" ]; then
        echo "  vorhanden: ${relative}"
        return 0
    fi

    mkdir -p "$(dirname "${target_path}")"
    cp -R "${source_path}" "${target_path}"
    echo "  kopiert:   ${relative}"
}

echo "Bootstrap für Worktree: ${WORKTREE_PATH}"
echo "Quellrepo:              ${REPO_PATH}"

copy_path "AGENTS.md"
copy_path ".kiloignore"
copy_path "docs/agent"
copy_path ".kilo/skills"
copy_path ".kilo/agent"
copy_path ".kilo/command"
copy_path "kilo.json"
copy_path "kilo.jsonc"

if [ ! -e "${WORKTREE_PATH}/.kilocoderules" ] && [ ! -L "${WORKTREE_PATH}/.kilocoderules" ]; then
    ln -s AGENTS.md "${WORKTREE_PATH}/.kilocoderules"
    echo "  Symlink:   .kilocoderules -> AGENTS.md"
else
    echo "  vorhanden: .kilocoderules"
fi

echo "Bootstrap abgeschlossen."
