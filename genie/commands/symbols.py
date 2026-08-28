"""Command-line boundary for canonical symbol queries."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Iterable

from genie.ghidra.database import AnalysisDatabase
from genie.ghidra.worklist import function_work_queue, render_work_queue
from genie.runtime import ROOT, default_rom, resolve
from genie.symbols import Symbol, SymbolStore, edit_symbol, mechanical_name


def _store() -> SymbolStore:
    return SymbolStore(root=ROOT)


def parse_symbol_address(value: str) -> int:
    try:
        parsed = int(str(value), 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"invalid address: {value}") from error
    if not 0 <= parsed <= 0xFFFFFF:
        raise argparse.ArgumentTypeError(f"address outside 24-bit space: {value}")
    return parsed


def _render(symbols: Iterable[Symbol], *, json_output: bool) -> None:
    values = list(symbols)
    if json_output:
        print(json.dumps([symbol.to_dict() for symbol in values], indent=2, sort_keys=True))
        return
    for symbol in values:
        aliases = f" aliases={','.join(symbol.aliases)}" if symbol.aliases else ""
        print(f"0x{symbol.address:08X}  {symbol.name}  [{symbol.kind}; {symbol.confidence}]{aliases}")


def command_symbols_show(args: argparse.Namespace) -> int:
    symbol = _store().at(args.address)
    if symbol is None:
        symbol = Symbol(
            address=args.address,
            name=mechanical_name(args.address),
            kind="unknown",
            source="generated",
        )
    if args.json_output:
        print(json.dumps(symbol.to_dict(), indent=2, sort_keys=True))
    else:
        print(f"address     0x{symbol.address:08X}")
        print(f"name        {symbol.name}")
        print(f"kind        {symbol.kind}")
        print(f"confidence  {symbol.confidence}")
        print(f"source      {symbol.source}")
        if symbol.provenance:
            print(f"provenance  {', '.join(symbol.provenance)}")
        if symbol.aliases:
            print(f"aliases     {', '.join(symbol.aliases)}")
        if symbol.range:
            print(f"range       0x{symbol.range[0]:08X}-0x{symbol.range[1]:08X}")
        if symbol.description:
            print(f"description {symbol.description}")
    return 0


def command_symbols_find(args: argparse.Namespace) -> int:
    matches = _store().find(args.query, kind=args.kind, exact=args.exact)
    _render(matches, json_output=args.json_output)
    return 0 if matches else 1


def command_symbols_list(args: argparse.Namespace) -> int:
    _render(_store().list(kind=args.kind), json_output=args.json_output)
    return 0


def command_symbols_validate(args: argparse.Namespace) -> int:
    rom = resolve(args.rom) if args.rom else default_rom()
    rom_size = rom.stat().st_size if rom.is_file() else None
    store = _store()
    errors = store.validate(rom_size=rom_size)
    if args.json_output:
        print(json.dumps({"valid": not errors, "errors": errors}, indent=2, sort_keys=True))
    elif errors:
        for error in errors:
            print(f"ERROR {error}")
    else:
        print(f"Validated {len(store.symbols)} symbols")
    return 1 if errors else 0


def command_symbols_stats(args: argparse.Namespace) -> int:
    stats: dict[str, Any] = _store().stats()
    if args.json_output:
        print(json.dumps(stats, indent=2, sort_keys=True))
    else:
        print(f"Symbols  {stats['total']}")
        for kind, count in stats["by_kind"].items():
            print(f"{kind:<10} {count}")
        print(f"aliases   {stats['aliases']}")
        print(f"ranged    {stats['ranged']}")
    return 0


def _function_queue(args: argparse.Namespace) -> list[dict[str, Any]]:
    if args.kind != "function":
        raise ValueError(f"symbols {args.symbols_command} currently supports --kind function only")
    database = AnalysisDatabase(resolve(args.database))
    database.load("metadata.json")
    return function_work_queue(
        database,
        _store(),
        coverage_path=resolve(args.coverage) if args.coverage else None,
    )


def command_symbols_unknown(args: argparse.Namespace) -> int:
    try:
        queue = _function_queue(args)
    except (OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR {error}")
        return 1
    limit = args.limit if args.limit > 0 else len(queue)
    render_work_queue(
        queue[:limit],
        total=len(queue),
        json_output=args.json_output,
        title="Unknown/mechanical functions",
    )
    return 0


def command_symbols_next(args: argparse.Namespace) -> int:
    try:
        queue = _function_queue(args)
    except (OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR {error}")
        return 1
    if not queue:
        print("No unknown/mechanical functions remain")
        return 1
    render_work_queue(queue[:1], total=len(queue), json_output=args.json_output, title="Next function")
    return 0


def _edit_command(args: argparse.Namespace, **changes: str | None) -> int:
    try:
        symbol = edit_symbol(
            args.address,
            root=ROOT,
            kind=args.kind,
            **changes,
        )
    except (OSError, TypeError, ValueError) as error:
        print(f"ERROR {error}")
        return 1
    if args.json_output:
        print(json.dumps(symbol.to_dict(), indent=2, sort_keys=True))
    else:
        print(f"Updated 0x{symbol.address:08X}: {symbol.name} [{symbol.kind}; {symbol.confidence}]")
    return 0


def command_symbols_rename(args: argparse.Namespace) -> int:
    return _edit_command(args, name=args.name)


def command_symbols_describe(args: argparse.Namespace) -> int:
    return _edit_command(args, description=args.description)


def command_symbols_confidence(args: argparse.Namespace) -> int:
    return _edit_command(args, confidence=args.confidence)


__all__ = [name for name in globals() if not name.startswith("__")]
