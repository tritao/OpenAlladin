#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${OPENALADDIN_BUILD_DIR:-${ROOT_DIR}/build}"
BINARY="${BUILD_DIR}/openaladdin_clean_core_client"

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
fi
cmake --build "${BUILD_DIR}" --target openaladdin_clean_core_client

exec "${BINARY}" "$@"
