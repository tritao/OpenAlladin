#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${OPENALADDIN_BUILD_DIR:-${ROOT_DIR}/build}"
BINARY="${BUILD_DIR}/openaladdin"
ASSET_DIR="${OPENALADDIN_ASSETS:-${ROOT_DIR}/build/assets/levels/level01}"
SPRITE_DIR="${OPENALADDIN_SPRITES:-${ROOT_DIR}/build/assets/sprites}"

# Always ask the configured build tree to update the native executable. CMake
# makes this incremental, so unchanged launches are cheap while source edits
# cannot leave run.sh using a stale binary. Fall back to build.sh when the
# build tree or generated assets need bootstrapping.
if [[ ! -x "${BINARY}" || ! -f "${BUILD_DIR}/CMakeCache.txt" \
    || ! -f "${ASSET_DIR}/background.ppm" \
    || ! -f "${SPRITE_DIR}/frames.json" ]] \
    || ! rg -q '"offset_pixels"' "${SPRITE_DIR}/frames.json"; then
    "${ROOT_DIR}/build.sh"
else
    cmake --build "${BUILD_DIR}" --target openaladdin
fi

exec "${BINARY}" --assets "${ASSET_DIR}" --sprites "${SPRITE_DIR}" "$@"
