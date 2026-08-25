#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${ROOT_DIR}/build/openaladdin"
ASSET_DIR="${OPENALADDIN_ASSETS:-${ROOT_DIR}/build/assets/levels/level01}"

if [[ ! -x "${BINARY}" || ! -f "${ASSET_DIR}/background.ppm" ]]; then
    "${ROOT_DIR}/build.sh"
fi

exec "${BINARY}" --assets "${ASSET_DIR}" "$@"
