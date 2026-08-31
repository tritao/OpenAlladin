"""Ghidra command handlers.

The implementation of the Ghidra workflow lives in :mod:`genie.ghidra`.
This module owns only the command-line boundary: argument normalization,
verification, and the small amount of rebuild orchestration that combines the
existing services.
"""

from __future__ import annotations

import argparse
import json

from genie.ghidra import (
    decompile_function,
    decompile_functions,
    rebuild_project,
    scan_project,
    setup_ghidra,
    verify_rom,
)
from genie.ghidra.context import build_context
from genie.ghidra.database import AnalysisDatabase, render_records
from genie.ghidra.validate import validate_database
from genie.ghidra.vm_writes import find_vm_writers
from genie.ghidra.worklist import symbol_review_queue
from genie.knowledge import validate_knowledge
from genie.runtime import ROOT, resolve
from genie.symbols import SymbolStore


def command_ghidra_setup(args: argparse.Namespace) -> int:
    """Install the pinned Ghidra release and its bundled PyGhidra package."""

    del args
    setup_ghidra()
    print("Setup complete.")
    return 0


def command_ghidra_verify(args: argparse.Namespace) -> int:
    """Verify the ROM selected for a Ghidra operation."""

    rom = resolve(args.rom)
    return verify_rom(rom, allow_unverified=args.allow_unverified)


def command_ghidra_rebuild(args: argparse.Namespace) -> int:
    """Rebuild the local Ghidra project and validate tracked knowledge."""

    rom = resolve(args.rom)
    status = rebuild_project(
        rom,
        allow_unverified=args.allow_unverified,
        reuse_project=args.reuse_project,
        no_analysis=args.no_analysis,
    )
    if status:
        return status

    errors = validate_knowledge(rom)
    if errors:
        for error in errors:
            print(f"ERROR {error}")
        return 1
    print("validated symbols and types")
    return 0


def command_ghidra_scan(args: argparse.Namespace) -> int:
    """Rebuild Ghidra and export the queryable whole-ROM database."""

    rom = resolve(args.rom)
    status = scan_project(
        rom,
        allow_unverified=args.allow_unverified,
        reuse_project=args.reuse_project,
        no_analysis=args.no_analysis,
    )
    if status:
        return status
    errors = validate_knowledge(rom)
    if errors:
        for error in errors:
            print(f"ERROR {error}")
        return 1
    print("validated symbols and types")
    return 0


def _database(args: argparse.Namespace) -> AnalysisDatabase:
    database = AnalysisDatabase(resolve(args.database))
    try:
        database.load("metadata.json")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(str(error)) from error
    return database


def _render_one(value: dict | None, *, json_output: bool) -> int:
    if value is None:
        print("No matching function")
        return 1
    if json_output:
        print(json.dumps(value, indent=2, sort_keys=True))
    else:
        for key, item in value.items():
            print(f"{key:<20} {item}")
    return 0


def command_ghidra_function(args: argparse.Namespace) -> int:
    return _render_one(_database(args).function(args.address), json_output=args.json_output)


def command_ghidra_callers(args: argparse.Namespace) -> int:
    render_records(_database(args).callers(args.address), json_output=args.json_output)
    return 0


def command_ghidra_callees(args: argparse.Namespace) -> int:
    render_records(_database(args).callees(args.address), json_output=args.json_output)
    return 0


def command_ghidra_writers(args: argparse.Namespace) -> int:
    render_records(_database(args).writers(args.address), json_output=args.json_output)
    return 0


def command_ghidra_readers(args: argparse.Namespace) -> int:
    render_records(_database(args).readers(args.address), json_output=args.json_output)
    return 0


def command_ghidra_vm_writers(args: argparse.Namespace) -> int:
    try:
        writers = find_vm_writers(
            resolve(args.rom),
            resolve(args.layout),
            args.address,
        )
    except (OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR {error}")
        return 1
    if args.json_output:
        print(json.dumps(writers, indent=2, sort_keys=True))
        return 0
    for writer in writers:
        print(
            f"{writer['address']} {writer['stream']} {writer['operation']}"
            f" {writer['target']}={writer['value']} [{writer['bytes']}]"
        )
    if not writers:
        print("No AnimationVM global-RAM writers found")
    return 0


def command_ghidra_xrefs(args: argparse.Namespace) -> int:
    render_records(_database(args).xrefs(args.address), json_output=args.json_output)
    return 0


def command_ghidra_decompile(args: argparse.Namespace) -> int:
    try:
        if args.review and args.address is not None:
            raise ValueError("ghidra decompile accepts an address or --review, not both")
        if not args.review and args.address is None:
            raise ValueError("ghidra decompile requires an address or --review")
        database = _database(args)
        cache_dir = resolve(args.cache_dir) if args.cache_dir else None
        project_dir = resolve(args.project_dir) if args.project_dir else None
        if args.review:
            queue = symbol_review_queue(database, SymbolStore(root=ROOT), kind="function")
            limit = args.limit if args.limit > 0 else len(queue)
            addresses = [int(item["address"], 0) for item in queue[:limit]]
            if not addresses:
                print("No semantic function review entries remain")
                return 1
            results = decompile_functions(
                addresses,
                database=database,
                cache_dir=cache_dir,
                project_dir=project_dir,
                force=args.force,
            )
        else:
            result = decompile_function(
                args.address,
                database=database,
                cache_dir=cache_dir,
                project_dir=project_dir,
                force=args.force,
            )
    except (OSError, TypeError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"ERROR {error}")
        return 1
    if args.review:
        if args.json_output:
            print(json.dumps({"count": len(results), "results": results}, indent=2, sort_keys=True))
        else:
            print(f"Decompiled {len(results)} semantic review functions")
            for item in results:
                print(f"{item['status'].capitalize()} {item['name']} ({item['address']})")
                print(f"Pseudocode: {item['path']}")
        return 0
    if args.json_output:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(f"{result['status'].capitalize()} {result['name']} ({result['address']})")
        print(f"Pseudocode: {result['path']}")
        if result.get("report"):
            print(f"Report:     {result['report']}")
    return 0


def _context_records(title: str, records: list[dict]) -> None:
    print(f"{title}  {len(records)}")
    for record in records:
        left = record.get("from")
        right = record.get("to")
        if left is not None or right is not None:
            print(
                f"  0x{int(str(left), 0):08X} -> 0x{int(str(right), 0):08X}"
                f" {record.get('type', '')}".rstrip()
            )
        else:
            print(f"  {json.dumps(record, sort_keys=True)}")


def _render_context(value: dict) -> None:
    print(f"address     {value['address']}")
    symbol = value.get("symbol") or value.get("function_symbol")
    if symbol:
        print(f"symbol      {symbol['name']} [{symbol['kind']}; {symbol['confidence']}]")
        if symbol.get("range"):
            print(
                f"symbol range 0x{symbol['range']['start']:08X}-0x{symbol['range']['end']:08X}"
            )
    function = value.get("function")
    if function:
        print(f"function    {function.get('name')}  {function.get('start', function.get('address'))}-{function.get('end', '')}")
    else:
        print("function    <none>")
    if value.get("layout"):
        layout = value["layout"]
        print(f"layout      {layout['class']}  {layout['start']}-{layout['end']} ({layout.get('name', '')})")
    if value.get("decompile"):
        decompile = value["decompile"]
        print(f"decompile   {'cached' if decompile['available'] else 'not cached'}  {decompile['path']}")
        if decompile.get("text"):
            print("decompile source")
            print(decompile["text"].rstrip())
    runtime = value.get("runtime")
    if runtime:
        scenarios = ", ".join(runtime.get("scenarios", [])) or "-"
        print(f"runtime     {'observed' if runtime.get('observed') else 'not observed'}; PCs {runtime.get('pc_count', 0)}; scenarios {scenarios}")
    _context_records("callers", value["callers"])
    _context_records("callees", value["callees"])
    _context_records("RAM reads", value["ram_reads"])
    _context_records("RAM writes", value["ram_writes"])
    _context_records("xrefs", value["xrefs"])
    _context_records("outgoing xrefs", value["outgoing_xrefs"])
    print(f"known symbols referenced  {len(value['known_symbols_referenced'])}")
    for symbol in value["known_symbols_referenced"]:
        print(f"  0x{symbol['address']:08X} {symbol['name']}")
    print(f"nearby layout objects    {len(value['nearby_layout'])}")
    for layout in value["nearby_layout"]:
        print(f"  {layout['start']}-{layout['end']} {layout['class']} {layout.get('name', '')}".rstrip())


def command_ghidra_context(args: argparse.Namespace) -> int:
    try:
        if args.review and args.address is not None:
            raise ValueError("ghidra context accepts an address or --review, not both")
        if not args.review and args.address is None:
            raise ValueError("ghidra context requires an address or --review")
        database = _database(args)
        common = {
            "layout_path": resolve(args.layout) if args.layout else None,
            "coverage_path": resolve(args.coverage) if args.coverage else None,
            "radius": args.radius,
            "include_decompile": args.include_decompile,
        }
        if args.review:
            queue = symbol_review_queue(database, SymbolStore(root=ROOT), kind="function")
            limit = args.limit if args.limit > 0 else len(queue)
            addresses = [int(item["address"], 0) for item in queue[:limit]]
            if not addresses:
                print("No semantic function review entries remain")
                return 1
            values = [
                build_context(database, address, SymbolStore(root=ROOT), **common)
                for address in addresses
            ]
        else:
            value = build_context(
                database,
                args.address,
                SymbolStore(root=ROOT),
                **common,
            )
    except (OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR {error}")
        return 1
    if args.review:
        if args.json_output:
            print(json.dumps({"count": len(values), "contexts": values}, indent=2, sort_keys=True))
        else:
            for index, review_value in enumerate(values):
                if index:
                    print("\n---\n")
                _render_context(review_value)
        return 0
    if args.json_output:
        print(json.dumps(value, indent=2, sort_keys=True))
    else:
        _render_context(value)
    return 0


def command_ghidra_unknown(args: argparse.Namespace) -> int:
    render_records(_database(args).unknown(), json_output=args.json_output)
    return 0


def command_ghidra_validate_db(args: argparse.Namespace) -> int:
    """Validate a generated whole-ROM database against known ROM facts."""

    database = AnalysisDatabase(resolve(args.database))
    try:
        errors = validate_database(database)
    except (OSError, ValueError, TypeError) as error:
        errors = [str(error)]
    if args.json_output:
        print(json.dumps({"valid": not errors, "errors": errors}, indent=2, sort_keys=True))
    elif errors:
        for error in errors:
            print(f"ERROR {error}")
    else:
        print("Validated whole-ROM analysis database")
    return 1 if errors else 0


__all__ = [
    "command_ghidra_rebuild",
    "command_ghidra_scan",
    "command_ghidra_function",
    "command_ghidra_callers",
    "command_ghidra_callees",
    "command_ghidra_writers",
    "command_ghidra_readers",
    "command_ghidra_vm_writers",
    "command_ghidra_xrefs",
    "command_ghidra_decompile",
    "command_ghidra_context",
    "command_ghidra_unknown",
    "command_ghidra_validate_db",
    "command_ghidra_setup",
    "command_ghidra_verify",
]
