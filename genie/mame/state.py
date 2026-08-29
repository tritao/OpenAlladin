"""Compatibility imports for the Aladdin MAME state services."""

from __future__ import annotations

import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[2]
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from genie.games.aladdin.mame import state as _canonical  # noqa: E402

# The original module deliberately exported single-underscore helpers through
# its dynamic __all__. Copy that surface so genie.api and older scripts remain
# source-compatible while the implementation lives under the game package.
for _name, _value in vars(_canonical).items():
    if not _name.startswith("__"):
        globals()[_name] = _value

__all__ = [name for name in vars(_canonical) if not name.startswith("__")]
