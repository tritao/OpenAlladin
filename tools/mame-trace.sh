#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAME_BIN="${MAME_BIN:-${ROOT_DIR}/external/mame/mame}"
ROM_FILE="${1:-${ROOT_DIR}/Disneys_Aladdin_U_p1.bin}"
TRACE_FRAMES="${OPENALADDIN_TRACE_FRAMES:-120}"
TRACE_DIR="${OPENALADDIN_TRACE_DIR:-${ROOT_DIR}/build/re/traces}"
SDL2_LIB_DIR="${ROOT_DIR}/build/deps/sdl2/sysroot/usr/lib/x86_64-linux-gnu"
VIDEO_MODE="${OPENALADDIN_MAME_VIDEO:-none}"

if [[ ! -x "${MAME_BIN}" ]]; then
    echo "MAME executable not found: ${MAME_BIN}" >&2
    echo "Build it with: make -C external/mame SUBTARGET=mame BUILDDIR=../../build/mame -j20" >&2
    exit 1
fi

if [[ ! -f "${ROM_FILE}" ]]; then
    echo "ROM file not found: ${ROM_FILE}" >&2
    exit 1
fi

mkdir -p "${TRACE_DIR}"
mkdir -p "${TRACE_DIR}/states" "${TRACE_DIR}/snapshots"

export OPENALADDIN_ROOT="${ROOT_DIR}"
export OPENALADDIN_TRACE_DIR="${TRACE_DIR}"
export OPENALADDIN_TRACE_FRAMES="${TRACE_FRAMES}"

if [[ -n "${OPENALADDIN_INPUT:-}" ]]; then
    export OPENALADDIN_INPUT
fi

export LD_LIBRARY_PATH="${SDL2_LIB_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

if [[ "${MAME_XVFB:-0}" == "1" && "${VIDEO_MODE}" == "none" ]]; then
    VIDEO_MODE="soft"
fi

LOAD_STATE="${OPENALADDIN_LOAD_STATE:-}"

MAME_ARGS=(
    genesis
    -cart "${ROM_FILE}"
    -autoboot_script "${ROOT_DIR}/re/mame/trace_boot.lua"
    -autoboot_delay 0
    -state_directory "${TRACE_DIR}/states"
    -snapshot_directory "${TRACE_DIR}/snapshots"
    -snapsize auto
    -skip_gameinfo
    -video "${VIDEO_MODE}"
    -sound none
    -nothrottle
)

if [[ "${OPENALADDIN_DEBUG_WATCH:-0}" == "1" || "${OPENALADDIN_TRACE_ACTOR_INIT:-0}" == "1" ]]; then
    MAME_ARGS+=(
        -debug
        -debugscript "${ROOT_DIR}/re/mame/continue-debugger.txt"
        -debuglog
    )
else
    MAME_ARGS+=( -debugger none )
fi

if [[ -n "${LOAD_STATE}" ]]; then
    MAME_ARGS+=( -state "${LOAD_STATE}" )
fi

if [[ "${MAME_XVFB:-0}" == "1" ]]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        echo "MAME_XVFB=1 requested, but xvfb-run is not installed" >&2
        exit 1
    fi
    exec xvfb-run -a -s "${MAME_XVFB_SERVER_ARGS:--screen 0 1024x768x24}" \
        env SDL_VIDEODRIVER=x11 "${MAME_BIN}" "${MAME_ARGS[@]}"
fi

exec "${MAME_BIN}" "${MAME_ARGS[@]}"
