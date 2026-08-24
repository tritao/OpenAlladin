#!/usr/bin/env bash
set -eu

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL_DIR="${GHIDRA_INSTALL_DIR:-$ROOT_DIR/.tools/ghidra-12.1.3}"

if [[ ! -x "$INSTALL_DIR/support/pyghidraRun" ]]; then
    echo "Ghidra is not installed at $INSTALL_DIR" >&2
    echo "Run: python tools/setup-ghidra.py" >&2
    exit 1
fi

export GHIDRA_INSTALL_DIR="$INSTALL_DIR"
export OPENALADDIN_ROOT="$ROOT_DIR"
export PYTHONPATH="$ROOT_DIR/tools${PYTHONPATH:+:$PYTHONPATH}"
if [[ -d "$ROOT_DIR/.tools/venv/bin" ]]; then
    export PATH="$ROOT_DIR/.tools/venv/bin:$PATH"
fi

exec "$INSTALL_DIR/support/pyghidraRun" "$@"
