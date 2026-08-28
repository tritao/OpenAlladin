"""Compatibility aliases for the original top-level setup commands."""

from __future__ import annotations

import argparse

from genie.commands.ghidra import command_ghidra_setup, command_ghidra_verify


def command_setup(args: argparse.Namespace) -> int:
    """Keep ``genie setup`` as an alias for ``genie ghidra setup``."""

    return command_ghidra_setup(args)


def command_verify(args: argparse.Namespace) -> int:
    """Keep ``genie verify`` as an alias for ``genie ghidra verify``."""

    return command_ghidra_verify(args)


__all__ = ["command_setup", "command_verify"]
