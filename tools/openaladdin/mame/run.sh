#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
MAME_BIN="${MAME_BIN:-${ROOT_DIR}/external/mame/mame}"
ROM_FILE="${1:-${ROOT_DIR}/rom/Disneys_Aladdin_U_p1.bin}"
TRACE_FRAMES="${OPENALADDIN_TRACE_FRAMES:-120}"
TRACE_DIR="${OPENALADDIN_TRACE_DIR:-${ROOT_DIR}/build/re/traces}"
SDL2_LIB_DIR="${ROOT_DIR}/build/deps/sdl2/sysroot/usr/lib/x86_64-linux-gnu"
VIDEO_MODE="${OPENALADDIN_MAME_VIDEO:-none}"
SOUND_MODE="${OPENALADDIN_MAME_SOUND:-none}"
HEADLESS="${OPENALADDIN_MAME_HEADLESS:-1}"
DEBUG_UI="${OPENALADDIN_MAME_DEBUG_UI:-0}"
MAME_XVFB="${MAME_XVFB:-0}"

# State-synchronized runs change into TRACE_DIR below. Normalize caller input
# first so a relative ROM path remains valid after that directory change.
if [[ "${ROM_FILE}" != /* ]]; then
    ROM_FILE="${ROOT_DIR}/${ROM_FILE}"
fi

# State-synchronized runs change into TRACE_DIR before launching MAME. Make a
# caller-supplied relative path absolute first so OPENALADDIN_TRACE_DIR does
# not become accidentally relative to itself after that directory change.
if [[ "${TRACE_DIR}" != /* ]]; then
    TRACE_DIR="${ROOT_DIR}/${TRACE_DIR}"
fi

# MAME's non-rendering backend expects an emulated time limit.  Keep it a
# little longer than the Lua frame limit so the harness remains authoritative,
# while still bounding a failed or stalled experiment.
TRACE_SECONDS=""
if [[ "${TRACE_FRAMES}" =~ ^[0-9]+$ ]]; then
    TRACE_SECONDS=$(( (TRACE_FRAMES + 59) / 60 + 1 ))
fi

if [[ ! -x "${MAME_BIN}" ]]; then
    echo "MAME executable not found: ${MAME_BIN}" >&2
    echo "Build it with: make -C external/mame SUBTARGET=mame BUILDDIR=../../build/mame -j20" >&2
    exit 1
fi

if [[ ! -f "${ROM_FILE}" ]]; then
    echo "ROM file not found: ${ROM_FILE}" >&2
    exit 1
fi

# Every trace header should carry the ROM identity, including direct
# run.sh invocations that bypass tools/oa.py. Allow an explicit value for
# controlled fixtures, but derive the verified hash by default.
if [[ -z "${OPENALADDIN_ROM_SHA256:-}" ]]; then
    OPENALADDIN_ROM_SHA256="$(sha256sum "${ROM_FILE}" | awk '{print $1}')"
fi
export OPENALADDIN_ROM_SHA256

# The Lua harness consumes a generated symbol table, while the YAML files are
# the canonical source. Regenerate this small derived file on every run so a
# clone with new tracked RAM symbols does not depend on a prior Ghidra import.
PYTHONPATH="${ROOT_DIR}/tools${PYTHONPATH:+:${PYTHONPATH}}" \
    python3 -c 'from openaladdin.common import ROOT, normalize_symbols, write_mame_symbols; write_mame_symbols(ROOT / "build/re/mame_symbols.lua", normalize_symbols())'

mkdir -p "${TRACE_DIR}"
mkdir -p "${TRACE_DIR}/states" "${TRACE_DIR}/snapshots"

export OPENALADDIN_ROOT="${ROOT_DIR}"
export OPENALADDIN_TRACE_DIR="${TRACE_DIR}"
export OPENALADDIN_TRACE_FRAMES="${TRACE_FRAMES}"

if [[ -n "${OPENALADDIN_INPUT:-}" ]]; then
    export OPENALADDIN_INPUT
fi

export LD_LIBRARY_PATH="${SDL2_LIB_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

if [[ "${MAME_XVFB}" == "1" ]]; then
    # Xvfb is an isolated display, so a real renderer is useful for captures
    # that need one without exposing a window on the developer's desktop.
    if [[ "${VIDEO_MODE}" == "none" ]]; then
        VIDEO_MODE="soft"
    fi
elif [[ "${HEADLESS}" == "1" ]]; then
    # MAME's "none" renderer still creates an SDL host window.  The dummy
    # SDL driver below is what makes the normal trace path truly headless.
    VIDEO_MODE="none"
fi

LOAD_STATE="${OPENALADDIN_LOAD_STATE:-}"

MAME_ARGS=(
    genesis
    -cart "${ROM_FILE}"
    -autoboot_script "${ROOT_DIR}/re/mame/lua/main.lua"
    -autoboot_delay 0
    -state_directory "${TRACE_DIR}/states"
    -snapshot_directory "${TRACE_DIR}/snapshots"
    -snapsize auto
    -skip_gameinfo
    -video "${VIDEO_MODE}"
    -sound "${SOUND_MODE}"
    -nothrottle
)

# A saved MAME machine state also restores the scheduler timers.  Restoring
# the external -seconds_to_run timer can therefore make a freshly loaded
# checkpoint exit on its first frame.  The Lua harness already has its own
# exact frame-limit shutdown, so loaded checkpoints do not need this second
# wall-clock bound.
if [[ -n "${TRACE_SECONDS}" && -z "${OPENALADDIN_LOAD_STATE:-}" ]]; then
    MAME_ARGS+=( -seconds_to_run "${TRACE_SECONDS}" )
fi

if [[ "${OPENALADDIN_DEBUG_WATCH:-0}" == "1" || "${OPENALADDIN_STATE_SYNC:-0}" == "1" || "${OPENALADDIN_TRACE_ACTOR_INIT:-0}" == "1" || "${OPENALADDIN_TRACE_RNC_LOADS:-0}" == "1" || "${OPENALADDIN_TRACE_EDGES:-0}" == "1" || "${OPENALADDIN_TRACE_AUDIO_COMMANDS:-0}" == "1" ]]; then
    MAME_ARGS+=(
        -debug
        -debugscript "${ROOT_DIR}/re/mame/lua/continue-debugger.txt"
        -debuglog
    )
fi

if [[ "${DEBUG_UI}" == "1" && "${HEADLESS}" != "1" ]]; then
    echo "OpenAladdin: MAME debugger UI enabled" >&2
else
    # Debug scripts and Lua write taps still work without opening a debugger
    # window during automated analysis.
    MAME_ARGS+=( -debugger none )
fi

if [[ "${OPENALADDIN_STATE_SYNC:-0}" == "1" || "${OPENALADDIN_TRACE_EDGES:-0}" == "1" ]]; then
    cd "${TRACE_DIR}"
fi

if [[ -n "${LOAD_STATE}" ]]; then
    MAME_ARGS+=( -state "${LOAD_STATE}" )
fi

if [[ "${MAME_XVFB}" == "1" ]]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        echo "MAME_XVFB=1 requested, but xvfb-run is not installed" >&2
        exit 1
    fi
    exec xvfb-run -a -s "${MAME_XVFB_SERVER_ARGS:--screen 0 1024x768x24}" \
        env SDL_VIDEODRIVER=x11 "${MAME_BIN}" "${MAME_ARGS[@]}"
fi

if [[ "${HEADLESS}" == "1" ]]; then
    # Do not inherit SDL_VIDEODRIVER=x11 (or another interactive backend)
    # from a desktop/debugging shell.  This is the actual no-window switch;
    # MAME's -nowindow option means fullscreen, not headless operation.
    exec env SDL_VIDEODRIVER=dummy "${MAME_BIN}" "${MAME_ARGS[@]}"
fi

exec "${MAME_BIN}" "${MAME_ARGS[@]}"
