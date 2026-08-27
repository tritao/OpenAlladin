"""Workspace setup and ROM verification commands."""

from __future__ import annotations

import argparse

from genie.runtime import *
from genie.knowledge import validate_knowledge
def command_setup(args: argparse.Namespace) -> int:
    return run_tool("ghidra/setup.py")

def command_verify(args: argparse.Namespace) -> int:
    rom = resolve(args.rom)
    forwarded = [str(rom)]
    if args.allow_unverified:
        forwarded.append("--allow-unverified")
    return run_tool("ghidra/verify.py", forwarded)

def command_ghidra_rebuild(args: argparse.Namespace) -> int:
    rom = resolve(args.rom)
    verify_args = [str(rom)]
    if args.allow_unverified:
        verify_args.append("--allow-unverified")
    status = run_tool("ghidra/verify.py", verify_args)
    if status:
        return status

    forwarded = [str(rom)]
    for flag in ("--allow-unverified", "--reuse-project", "--no-analysis"):
        if getattr(args, flag[2:].replace("-", "_")):
            forwarded.append(flag)
    status = run_tool("ghidra/import_rom.py", forwarded)
    if status:
        return status
    errors = validate_knowledge(rom)
    if errors:
        for error in errors:
            print(f"ERROR {error}")
        return 1
    print("validated symbols and types")
    return 0

__all__ = [name for name in globals() if not name.startswith("__")]
