"""Compatibility entry point for Aladdin's MAME coverage-gap report."""

from __future__ import annotations

import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[2]
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from genie.games.aladdin.mame import coverage_gaps as _canonical  # noqa: E402

for _name, _value in vars(_canonical).items():
    if not _name.startswith("__"):
        globals()[_name] = _value

__all__ = [name for name in vars(_canonical) if not name.startswith("__")]

if __name__ == "__main__":
    raise SystemExit(_canonical.main())
