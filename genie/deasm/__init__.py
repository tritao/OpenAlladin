"""Offline, deterministic ROM deassembly generation services."""

from .emitter import DeasmError, EmissionResult, emit, load_input, validate_input
from .model import DeasmInput, InstructionRecord
from .stats import DeasmStats, collect
from .toolchain import (
    AssemblyError,
    AssemblyResult,
    M68kToolchain,
    ToolchainError,
    assemble,
    find_toolchain,
)

__all__ = [
    "DeasmError",
    "DeasmInput",
    "DeasmStats",
    "EmissionResult",
    "InstructionRecord",
    "collect",
    "AssemblyError",
    "AssemblyResult",
    "M68kToolchain",
    "ToolchainError",
    "assemble",
    "emit",
    "find_toolchain",
    "load_input",
    "validate_input",
]
