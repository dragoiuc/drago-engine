#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

GAME_NAME="${GAME_NAME:-finalGame}"
BUILD_DIR="${1:-${ROOT_DIR}/build-linux}"
RELEASE_DIR="${2:-${ROOT_DIR}/dist/${GAME_NAME}-linux}"
PACKAGE_TEMPLATE_DIR="${ROOT_DIR}/packaging/linux"
SOURCE_GAME_DIR="${ROOT_DIR}/resources/${GAME_NAME}"
RELEASE_GAME_DIR="${RELEASE_DIR}/resources/${GAME_NAME}"
SOURCE_BINARY="${BUILD_DIR}/game_engine_dragoiuc"
LEGACY_ZIP_PATH="${RELEASE_DIR}/resources/${GAME_NAME}.zip"

if [[ ! -x "${SOURCE_BINARY}" ]]; then
    echo "Missing executable: ${SOURCE_BINARY}" >&2
    echo "Build the Linux target first, for example:" >&2
    echo "  cmake -S . -B build-linux" >&2
    echo "  cmake --build build-linux -j" >&2
    exit 1
fi

if [[ ! -d "${SOURCE_GAME_DIR}" ]]; then
    echo "Missing game resources: ${SOURCE_GAME_DIR}" >&2
    exit 1
fi

mkdir -p "${RELEASE_DIR}" "${RELEASE_DIR}/resources"

install -m 755 "${SOURCE_BINARY}" "${RELEASE_DIR}/game_engine_dragoiuc"
install -m 755 "${PACKAGE_TEMPLATE_DIR}/launch.sh" "${RELEASE_DIR}/launch.sh"
install -m 644 "${PACKAGE_TEMPLATE_DIR}/.itch.toml" "${RELEASE_DIR}/.itch.toml"
install -m 644 "${PACKAGE_TEMPLATE_DIR}/README.md" "${RELEASE_DIR}/README.md"

rsync -a --delete "${SOURCE_GAME_DIR}/" "${RELEASE_GAME_DIR}/"

if [[ -f "${LEGACY_ZIP_PATH}" ]]; then
    rm -f "${LEGACY_ZIP_PATH}"
fi

echo "Linux release packaged at ${RELEASE_DIR}"
