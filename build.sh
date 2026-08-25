#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROM_PATH="${OPENALADDIN_ROM:-${ROOT_DIR}/rom/Disneys_Aladdin_U_p1.bin}"
ASSET_DIR="${OPENALADDIN_ASSETS:-${ROOT_DIR}/build/assets/levels/level01}"

if [[ ! -f "${ROM_PATH}" ]]; then
    echo "build.sh: ROM not found: ${ROM_PATH}" >&2
    exit 1
fi

# Generated assets are ignored by Git. Recreate them automatically on a fresh
# checkout or when the runtime-friendly level render is missing.
if [[ ! -f "${ASSET_DIR}/background.ppm" ]]; then
    echo "build.sh: extracting assets from ${ROM_PATH}"
    python3 "${ROOT_DIR}/tools/oa.py" assets --rom "${ROM_PATH}"
fi

echo "build.sh: building OpenAladdin"
make -C "${ROOT_DIR}/src"
echo "build.sh: build complete: ${ROOT_DIR}/build/openaladdin"
