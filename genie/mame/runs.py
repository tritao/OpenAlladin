"""Compatibility imports for the Aladdin MAME run services."""

from __future__ import annotations

from genie.games.aladdin.mame import runs as _canonical

for _name, _value in vars(_canonical).items():
    if not _name.startswith("__"):
        globals()[_name] = _value

__all__ = [name for name in vars(_canonical) if not name.startswith("__")]
