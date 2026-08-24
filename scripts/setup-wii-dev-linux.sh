#!/usr/bin/env bash
set -euo pipefail

if ! command -v dkp-pacman &>/dev/null && ! command -v pacman &>/dev/null; then
	echo "Error: pacman not found."
	echo "Install devkitPro from https://devkitpro.org/wiki/Getting_Started"
	exit 1
fi

PACMAN_CMD="dkp-pacman"
if ! command -v dkp-pacman &>/dev/null; then
	PACMAN_CMD="pacman"
fi

if [[ "$(id -u)" -eq 0 ]]; then
	SUDO=""
else
	SUDO="sudo"
fi

${SUDO} "${PACMAN_CMD}" -Syu --needed --noconfirm wii-dev
${SUDO} "${PACMAN_CMD}" -S --needed --noconfirm ppc-mpg123
# Host-side tools used by scripts/build-wii.sh (powerpc-eabi-cmake wraps cmake)
${SUDO} "${PACMAN_CMD}" -S --needed --noconfirm cmake make

echo "Done. Run ./scripts/build-wii.sh to compile."
