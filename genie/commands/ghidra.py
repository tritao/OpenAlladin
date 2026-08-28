"""Ghidra command handlers.

The implementation of the Ghidra workflow lives in :mod:`genie.ghidra`.
This module owns only the command-line boundary: argument normalization,
verification, and the small amount of rebuild orchestration that combines the
existing services.
"""

from __future__ import annotations

import argparse

from genie.ghidra import rebuild_project, setup_ghidra, verify_rom
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


__all__ = [
    "command_ghidra_rebuild",
    "command_ghidra_setup",
    "command_ghidra_verify",
]
