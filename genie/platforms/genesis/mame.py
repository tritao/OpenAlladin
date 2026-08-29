"""Genesis-specific MAME identifiers used by the Lua harness."""

from __future__ import annotations

from .input import INPUT_MAPPING

MACHINE = "genesis"
CONTROLLER_PORT = ":ctrl1:mdpad:PAD"
CONTROLLER_MAPPING = INPUT_MAPPING


__all__ = ["CONTROLLER_MAPPING", "CONTROLLER_PORT", "MACHINE"]
