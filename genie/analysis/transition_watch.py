"""Compatibility import for the relocated Aladdin transition analysis."""

import sys
from pathlib import Path


_ROOT = Path(__file__).resolve().parents[2]
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from genie.games.aladdin.analysis.transition_watch import *  # noqa: F401,F403


if __name__ == "__main__":
    raise SystemExit(main())
