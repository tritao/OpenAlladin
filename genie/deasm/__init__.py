"""Offline, deterministic ROM deassembly generation services."""

from .emitter import DeasmError, EmissionResult, emit, load_input, validate_input
from .model import DeasmInput, InstructionRecord
from .stats import DeasmStats, collect

__all__ = [
    "DeasmError",
    "DeasmInput",
    "DeasmStats",
    "EmissionResult",
    "InstructionRecord",
    "collect",
    "emit",
    "load_input",
    "validate_input",
]
