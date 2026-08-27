"""Genie reverse-engineering platform for OpenAladdin."""

from __future__ import annotations

import importlib
import os
from pathlib import Path
import sys

__version__ = "0.1.0"


def _install_legacy_import_alias() -> None:
    """Keep absolute imports in the existing tool modules working.

    The original modules were executed with ``tools`` on ``PYTHONPATH`` and
    therefore imported their package as top-level ``openaladdin``.  Genie
    packages those modules under ``tools.openaladdin`` while the command
    decomposition is staged, so this alias preserves their import contract.
    """

    if "openaladdin" in sys.modules:
        return

    configured_root = os.environ.get("OPENALADDIN_ROOT")
    starts = [Path(configured_root)] if configured_root else []
    starts.extend((Path.cwd(), Path(__file__).resolve()))
    for start in starts:
        candidate = start.resolve()
        if candidate.is_file():
            candidate = candidate.parent
        for root in (candidate, *candidate.parents):
            tools_dir = root / "tools"
            if (tools_dir / "openaladdin").is_dir():
                sys.path.insert(0, str(tools_dir))
                sys.modules["openaladdin"] = importlib.import_module("openaladdin")
                return

    # A package-only installation still exposes the old modules under the
    # packaged ``tools`` namespace for callers that do not have a checkout.
    sys.modules["openaladdin"] = importlib.import_module("tools.openaladdin")


_install_legacy_import_alias()

__all__ = ["__version__"]
