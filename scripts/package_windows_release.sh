#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

GAME_NAME="${GAME_NAME:-finalGame}"
GENERATOR="${GENERATOR:-Visual Studio 17 2022}"
ARCHITECTURE="${ARCHITECTURE:-x64}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-windows-vs}"
RELEASE_DIR="${RELEASE_DIR:-${ROOT_DIR}/dist/${GAME_NAME}-windows}"
PACKAGE_TEMPLATE_DIR="${ROOT_DIR}/packaging/windows"
WIN_CMAKE="${WIN_CMAKE:-cmake.exe}"

detect_windows_cmake() {
    local candidate=""

    if command -v "${WIN_CMAKE}" >/dev/null 2>&1; then
        command -v "${WIN_CMAKE}"
        return 0
    fi

    for candidate in \
        "/mnt/c/Program Files/CMake/bin/cmake.exe" \
        "/mnt/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
        "/mnt/c/Program Files/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
        "/mnt/c/Program Files/Microsoft Visual Studio/17/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
        "/mnt/c/Program Files/Microsoft Visual Studio/17/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
        "/mnt/c/Program Files (x86)/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
        "/mnt/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
    do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done

    return 1
}

if ! command -v wslpath >/dev/null 2>&1; then
    echo "This script must be run inside WSL." >&2
    exit 1
fi

if ! WIN_CMAKE_PATH="$(detect_windows_cmake)"; then
    echo "Could not find a Windows cmake.exe." >&2
    echo "Install Windows CMake, or install Visual Studio C++ tools with CMake support." >&2
    exit 1
fi

ROOT_DIR_WIN="$(wslpath -w "${ROOT_DIR}")"
BUILD_DIR_WIN="$(wslpath -w "${BUILD_DIR}")"
RELEASE_DIR_WIN="$(wslpath -w "${RELEASE_DIR}")"

"${WIN_CMAKE_PATH}" -S "${ROOT_DIR_WIN}" -B "${BUILD_DIR_WIN}" \
    -G "${GENERATOR}" -A "${ARCHITECTURE}" \
    -DWINDOWS_RELEASE_GAME="${GAME_NAME}"

"${WIN_CMAKE_PATH}" --build "${BUILD_DIR_WIN}" --config Release
"${WIN_CMAKE_PATH}" --install "${BUILD_DIR_WIN}" --config Release --prefix "${RELEASE_DIR_WIN}"

mkdir -p "${RELEASE_DIR}"
install -m 755 "${PACKAGE_TEMPLATE_DIR}/launch.bat" "${RELEASE_DIR}/launch.bat"
install -m 644 "${PACKAGE_TEMPLATE_DIR}/.itch.toml" "${RELEASE_DIR}/.itch.toml"
install -m 644 "${PACKAGE_TEMPLATE_DIR}/README.md" "${RELEASE_DIR}/README.md"

echo "Windows release packaged at ${RELEASE_DIR}"
