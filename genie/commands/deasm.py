"""Command-line boundary for offline ROM deassembly generation."""

from __future__ import annotations

import argparse
import json

from genie.deasm import DeasmError, collect, emit, load_input
from genie.runtime import ROOT, resolve
from genie.symbols import SymbolStore


DEFAULT_DATABASE = ROOT / "build/re/full-rom"
DEFAULT_OUTPUT = ROOT / "build/re/deasm/aladdin.asm"


def _input(args: argparse.Namespace):
    database = resolve(args.database)
    layout = resolve(args.layout) if args.layout else database / "layout.json"
    instructions = resolve(args.instructions) if args.instructions else database / "instructions.json"
    return load_input(resolve(args.rom), layout, instructions)


def _symbols() -> SymbolStore:
    return SymbolStore(root=ROOT)


def command_deasm_build(args: argparse.Namespace) -> int:
    try:
        value = _input(args)
        result = emit(value, _symbols())
    except (DeasmError, OSError, TypeError, ValueError) as error:
        print(f"ERROR {error}")
        return 1
    output = resolve(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(result.source, encoding="utf-8")
    print(f"Generated complete ROM deassembly: {result.owned_bytes:,} bytes -> {output}")
    return 0


def command_deasm_stats(args: argparse.Namespace) -> int:
    try:
        value = _input(args)
        stats = collect(value, _symbols())
    except (DeasmError, OSError, TypeError, ValueError) as error:
        print(f"ERROR {error}")
        return 1
    if args.json_output:
        print(json.dumps(stats.to_dict(), indent=2, sort_keys=True))
    else:
        print(stats.render())
    return 0


__all__ = ["command_deasm_build", "command_deasm_stats"]
