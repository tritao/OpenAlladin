"""Allow Genie to be invoked with ``python -m genie``."""

from __future__ import annotations

from .cli import main


if __name__ == "__main__":
    raise SystemExit(main())
