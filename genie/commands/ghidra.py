"""Ghidra command handlers.

The implementation of the Ghidra workflow lives in :mod:`genie.ghidra`.
This module owns only the command-line boundary: argument normalization,
verification, and the small amount of rebuild orchestration that combines the
existing services.
"""

from __future__ import annotations

import argparse
import json

from genie.ghidra import rebuild_project, scan_project, setup_ghidra, verify_rom
from genie.ghidra.database import AnalysisDatabase, render_records
from genie.ghidra.validate import validate_database
from genie.knowledge import validate_knowledge
from genie.runtime import resolve


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


def command_ghidra_xrefs(args: argparse.Namespace) -> int:
    render_records(_database(args).xrefs(args.address), json_output=args.json_output)
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
    "command_ghidra_xrefs",
    "command_ghidra_unknown",
    "command_ghidra_validate_db",
    "command_ghidra_setup",
    "command_ghidra_verify",
]
