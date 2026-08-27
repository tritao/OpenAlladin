"""Ghidra tooling exposed through Genie during the package migration."""

from __future__ import annotations

from pathlib import Path

from openaladdin import ghidra as _legacy

__path__ = [str(Path(_legacy.__file__).resolve().parent)]
__doc__ = _legacy.__doc__


def __getattr__(name: str):
    return getattr(_legacy, name)
