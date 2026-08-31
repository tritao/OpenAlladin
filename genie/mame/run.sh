#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MAME_BIN="${MAME_BIN:-${ROOT_DIR}/external/mame/mame}"
ROM_FILE="${1:-${ROOT_DIR}/rom/Disneys_Aladdin_U_p1.bin}"
TRACE_FRAMES="${OPENALADDIN_TRACE_FRAMES:-120}"
TRACE_DIR="${OPENALADDIN_TRACE_DIR:-${ROOT_DIR}/build/re/traces}"
SDL2_LIB_DIR="${ROOT_DIR}/build/deps/sdl2/sysroot/usr/lib/x86_64-linux-gnu"
VIDEO_MODE="${OPENALADDIN_MAME_VIDEO:-none}"
EXECUTION_PROFILE="${OPENALADDIN_EXECUTION_PROFILE:-analysis}"
if [[ "${EXECUTION_PROFILE}" != "analysis" && "${EXECUTION_PROFILE}" != "interactive" ]]; then
    echo "OPENALADDIN_EXECUTION_PROFILE must be analysis or interactive" >&2
    exit 1
fi
if [[ -n "${OPENALADDIN_MAME_SOUND:-}" ]]; then
    SOUND_MODE="${OPENALADDIN_MAME_SOUND}"
elif [[ "${EXECUTION_PROFILE}" == "interactive" ]]; then
    SOUND_MODE="sdl"
else
    SOUND_MODE="none"
fi
HEADLESS="${OPENALADDIN_MAME_HEADLESS:-1}"
DEBUG_UI="${OPENALADDIN_MAME_DEBUG_UI:-0}"
MAME_XVFB="${MAME_XVFB:-0}"
STATE_DIRECTORY="${OPENALADDIN_STATE_DIRECTORY:-${TRACE_DIR}/states}"
RECORD_FILE="${OPENALADDIN_RECORD_FILE:-}"
PLAYBACK_FILE="${OPENALADDIN_PLAYBACK_FILE:-}"
INPUT_DIRECTORY="${OPENALADDIN_INPUT_DIRECTORY:-}"

if [[ -n "${RECORD_FILE}" && -n "${PLAYBACK_FILE}" ]]; then
    echo "OPENALADDIN_RECORD_FILE and OPENALADDIN_PLAYBACK_FILE are mutually exclusive" >&2
    exit 1
fi

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

if [[ "${STATE_DIRECTORY}" != /* ]]; then
    STATE_DIRECTORY="${ROOT_DIR}/${STATE_DIRECTORY}"
fi

if [[ -n "${RECORD_FILE}" && "${RECORD_FILE}" != /* ]]; then
    RECORD_FILE="${ROOT_DIR}/${RECORD_FILE}"
fi

if [[ -n "${PLAYBACK_FILE}" && "${PLAYBACK_FILE}" != /* ]]; then
    PLAYBACK_FILE="${ROOT_DIR}/${PLAYBACK_FILE}"
fi

# MAME resolves -record/-playback through its input directory and treats the
# argument as a filename.  Keep the public environment API path-shaped while
# passing MAME a directory plus basename.
if [[ -n "${RECORD_FILE}" || -n "${PLAYBACK_FILE}" ]]; then
    if [[ -z "${INPUT_DIRECTORY}" ]]; then
        if [[ -n "${RECORD_FILE}" ]]; then
            INPUT_DIRECTORY="$(dirname "${RECORD_FILE}")"
        else
            INPUT_DIRECTORY="$(dirname "${PLAYBACK_FILE}")"
        fi
    elif [[ "${INPUT_DIRECTORY}" != /* ]]; then
        INPUT_DIRECTORY="${ROOT_DIR}/${INPUT_DIRECTORY}"
    fi
    if [[ -n "${RECORD_FILE}" ]]; then
        RECORD_FILE="$(basename "${RECORD_FILE}")"
    fi
    if [[ -n "${PLAYBACK_FILE}" ]]; then
        PLAYBACK_FILE="$(basename "${PLAYBACK_FILE}")"
    fi
fi

# MAME's non-rendering backend expects an emulated time limit.  Keep it a
# little longer than the Lua frame limit so the harness remains authoritative,
# while still bounding a failed or stalled experiment.
TRACE_SECONDS=""
if [[ "${TRACE_FRAMES}" =~ ^[0-9]+$ ]]; then
    TRACE_SECONDS=$(( (TRACE_FRAMES + 59) / 60 + 1 ))
fi

# MAME save states can restore the emulated-time position together with the
# machine.  A state captured after a finite -seconds_to_run budget may
# therefore exit immediately when loaded, before the Lua frame limit gets a
# chance to run.  Keep the normal finite bound for fresh boots, but give
# loaded-state traces an explicit ceiling that is safely above any recorded
# checkpoint.  The Lua harness still owns the exact frame count.
LOADED_STATE_SECONDS="${OPENALADDIN_MAME_LOADED_STATE_SECONDS:-100000}"

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
# run.sh invocations that bypass the Genie CLI. Allow an explicit value for
# controlled fixtures, but derive the verified hash by default.
if [[ -z "${OPENALADDIN_ROM_SHA256:-}" ]]; then
    OPENALADDIN_ROM_SHA256="$(sha256sum "${ROM_FILE}" | awk '{print $1}')"
fi
export OPENALADDIN_ROM_SHA256

# The Lua harness consumes a generated symbol table, while the YAML files are
# the canonical source. Regenerate this small derived file on every run so a
# clone with new tracked RAM symbols does not depend on a prior Ghidra import.
SYMBOL_FILE="${ROOT_DIR}/build/re/mame_symbols.lua"
SYMBOL_TEMP="${SYMBOL_FILE}.tmp.$$"
PYTHONPATH="${ROOT_DIR}${PYTHONPATH:+:${PYTHONPATH}}" \
    OPENALADDIN_SYMBOL_OUTPUT="${SYMBOL_TEMP}" \
    python3 -c 'import os; from pathlib import Path; from genie.common import normalize_symbols, write_mame_symbols; write_mame_symbols(Path(os.environ["OPENALADDIN_SYMBOL_OUTPUT"]), normalize_symbols())' \
    && mv -f "${SYMBOL_TEMP}" "${SYMBOL_FILE}"

mkdir -p "${TRACE_DIR}"
mkdir -p "${STATE_DIRECTORY}" "${TRACE_DIR}/snapshots"

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
if [[ -n "${LOAD_STATE}" && "${LOAD_STATE}" != /* ]]; then
    LOAD_STATE="${ROOT_DIR}/${LOAD_STATE}"
fi

MAME_ARGS=(
    genesis
    -cart "${ROM_FILE}"
    -autoboot_script "${ROOT_DIR}/re/mame/lua/main.lua"
    -autoboot_delay 0
    -state_directory "${STATE_DIRECTORY}"
    -snapshot_directory "${TRACE_DIR}/snapshots"
    -snapsize auto
    -skip_gameinfo
    -video "${VIDEO_MODE}"
    -sound "${SOUND_MODE}"
)

if [[ "${HEADLESS}" != "1" ]]; then
    # Force a normal play session into a window even when the user's MAME
    # configuration has fullscreen enabled. Headless sessions use SDL's dummy
    # driver and do not need a display-mode flag.
    MAME_ARGS+=( -window )
fi

if [[ "${EXECUTION_PROFILE}" == "analysis" ]]; then
    MAME_ARGS+=( -nothrottle )
fi

if [[ -n "${RECORD_FILE}" ]]; then
    MAME_ARGS+=( -input_directory "${INPUT_DIRECTORY}" -record "${RECORD_FILE}" )
fi

if [[ -n "${PLAYBACK_FILE}" ]]; then
    MAME_ARGS+=( -input_directory "${INPUT_DIRECTORY}" -playback "${PLAYBACK_FILE}" )
fi

# A saved MAME machine state also restores the scheduler timers.  Restoring
# the external -seconds_to_run timer can therefore make a freshly loaded
# checkpoint exit on its first frame.  The Lua harness already has its own
# exact frame-limit shutdown, so loaded checkpoints do not need this second
# wall-clock bound.
if [[ -n "${OPENALADDIN_LOAD_STATE:-}" || -n "${OPENALADDIN_PRELOAD_STATE:-}" ]]; then
    MAME_ARGS+=( -seconds_to_run "${LOADED_STATE_SECONDS}" )
elif [[ -n "${TRACE_SECONDS}" ]]; then
    MAME_ARGS+=( -seconds_to_run "${TRACE_SECONDS}" )
fi

if [[ "${OPENALADDIN_DEBUG_WATCH:-0}" == "1" || "${OPENALADDIN_STATE_SYNC:-0}" == "1" || "${OPENALADDIN_TRACE_ACTOR_INIT:-0}" == "1" || "${OPENALADDIN_TRACE_RNC_LOADS:-0}" == "1" || "${OPENALADDIN_TRACE_EDGES:-0}" == "1" || "${OPENALADDIN_TRACE_AUDIO_COMMANDS:-0}" == "1" || "${OPENALADDIN_TRACE_SCHEDULER:-0}" == "1" || "${OPENALADDIN_TRACE_SCHEDULER_CALLS:-0}" == "1" || -n "${OPENALADDIN_BREAKPOINTS:-}" ]]; then
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

# Keep debugger output beside the trace that requested it.  Without this,
# MAME's implicit -debuglog path is the caller's working directory, which can
# leave a stale or unrelated debug.log at the repository root for breakpoint
# and actor-initializer runs.
if [[ "${OPENALADDIN_STATE_SYNC:-0}" == "1" || "${OPENALADDIN_TRACE_EDGES:-0}" == "1" || "${OPENALADDIN_DEBUG_WATCH:-0}" == "1" || "${OPENALADDIN_TRACE_ACTOR_INIT:-0}" == "1" || "${OPENALADDIN_TRACE_RNC_LOADS:-0}" == "1" || "${OPENALADDIN_TRACE_AUDIO_COMMANDS:-0}" == "1" || "${OPENALADDIN_TRACE_SCHEDULER:-0}" == "1" || "${OPENALADDIN_TRACE_SCHEDULER_CALLS:-0}" == "1" || -n "${OPENALADDIN_BREAKPOINTS:-}" ]]; then
    cd "${TRACE_DIR}"
fi

if [[ -n "${LOAD_STATE}" ]]; then
    if [[ -f "${LOAD_STATE}" ]]; then
        # MAME's -state option takes a state name, not a filesystem path.
        # Load an explicit checkpoint through the Lua API at frame 1 so the
        # trace can use paths outside MAME's state directory as well.
        export OPENALADDIN_PRELOAD_STATE="${LOAD_STATE}"
    else
        MAME_ARGS+=( -state "${LOAD_STATE}" )
    fi
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
