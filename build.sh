#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROM_PATH="${OPENALADDIN_ROM:-${ROOT_DIR}/rom/Disneys_Aladdin_U_p1.bin}"
ASSET_DIR="${OPENALADDIN_ASSETS:-${ROOT_DIR}/build/assets/levels/level01}"
SPRITE_DIR="${OPENALADDIN_SPRITES:-${ROOT_DIR}/build/assets/sprites}"
BUILD_DIR="${OPENALADDIN_BUILD_DIR:-${ROOT_DIR}/build}"

if [[ ! -f "${ROM_PATH}" ]]; then
    echo "build.sh: ROM not found: ${ROM_PATH}" >&2
    exit 1
fi

# Generated assets are ignored by Git. Recreate them automatically on a fresh
# checkout, when the runtime-friendly level render is missing, or when an old
# pre-frame-origin sprite manifest is still present locally.
if [[ ! -f "${ASSET_DIR}/background.ppm" || ! -f "${SPRITE_DIR}/frames.json" ]] \
    || ! rg -q '"offset_pixels"' "${SPRITE_DIR}/frames.json" \
    || ! rg -q '"tile_order": "column-major"' "${SPRITE_DIR}/frames.json"; then
    echo "build.sh: extracting assets from ${ROM_PATH}"
    python3 -m genie assets --rom "${ROM_PATH}"
fi

echo "build.sh: building OpenAladdin"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}" --target openaladdin
echo "build.sh: build complete: ${BUILD_DIR}/openaladdin"
