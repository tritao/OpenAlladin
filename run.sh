#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${ROOT_DIR}/build/openaladdin"
ASSET_DIR="${OPENALADDIN_ASSETS:-${ROOT_DIR}/build/assets/levels/level01}"
SPRITE_DIR="${OPENALADDIN_SPRITES:-${ROOT_DIR}/build/assets/sprites}"

if [[ ! -x "${BINARY}" || ! -f "${ASSET_DIR}/background.ppm" \
    || ! -f "${SPRITE_DIR}/frames.json" ]] \
    || ! rg -q '"offset_pixels"' "${SPRITE_DIR}/frames.json"; then
    "${ROOT_DIR}/build.sh"
fi

exec "${BINARY}" --assets "${ASSET_DIR}" --sprites "${SPRITE_DIR}" "$@"
