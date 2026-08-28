"""Assembler dialect backends for generated deassembly source."""

from .gnu import Gnu68000Syntax

__all__ = ["Gnu68000Syntax"]
