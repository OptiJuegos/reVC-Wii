#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build-wii"

# Prefer the Wii CMake wrapper when it is on PATH or in the usual install roots.
CMAKE_BIN=""
if command -v powerpc-eabi-cmake &>/dev/null; then
	CMAKE_BIN="$(command -v powerpc-eabi-cmake)"
elif [[ -x /opt/devkitpro/portlibs/wii/bin/powerpc-eabi-cmake ]]; then
	CMAKE_BIN=/opt/devkitpro/portlibs/wii/bin/powerpc-eabi-cmake
elif [[ -x /c/devkitPro/portlibs/wii/bin/powerpc-eabi-cmake ]]; then
	CMAKE_BIN=/c/devkitPro/portlibs/wii/bin/powerpc-eabi-cmake
elif [[ -x /c/msys64/opt/devkitpro/portlibs/wii/bin/powerpc-eabi-cmake ]]; then
	CMAKE_BIN=/c/msys64/opt/devkitpro/portlibs/wii/bin/powerpc-eabi-cmake
fi

if [[ -z "${CMAKE_BIN}" ]]; then
	echo "Error: powerpc-eabi-cmake not found."
	echo "Install wii-dev (see requirements.txt / scripts/setup-wii-dev-*)."
	exit 1
fi

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "Using cmake: ${CMAKE_BIN}"
"${CMAKE_BIN}" -S "${ROOT}" -B "${BUILD_DIR}" \
	-DREVC_VENDORED_LIBRW=ON \
	-DWII_GAME_BOOT=ON
cmake --build "${BUILD_DIR}" -j"${JOBS}"

echo
echo "Build complete: ${BUILD_DIR}/src/reVC.dol"
echo "Copy reVC.dol (do NOT rename) to sd:/apps/reVC/ with GTA VC assets."
echo "Test in Dolphin first, then Homebrew Channel."
