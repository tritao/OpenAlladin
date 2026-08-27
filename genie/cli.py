"""Genie's temporary compatibility frontend.

The existing OA parser is loaded as the single legacy backend for this
packaging milestone.  Keeping the adapter here means the eventual command
modules can replace individual legacy handlers without creating a second
CLI implementation.
"""

from __future__ import annotations

from contextlib import contextmanager
import os
from pathlib import Path
import runpy
import sys
from collections.abc import Iterator, Sequence
from typing import Any

from .context import find_repository_root


def _legacy_frontend() -> Path:
    """Resolve the checkout's legacy frontend for source or installed use."""

    configured_root = os.environ.get("OPENALADDIN_ROOT")
    starts = [Path(configured_root)] if configured_root else []
    starts.extend((Path.cwd(), Path(__file__).resolve()))
    for start in starts:
        root = find_repository_root(start)
        frontend = root / "tools" / "oa.py"
        if frontend.is_file():
            return frontend
    raise RuntimeError(
        "could not find an OpenAladdin checkout; run Genie from the repository "
        "or set OPENALADDIN_ROOT"
    )


@contextmanager
def _legacy_invocation(program: str, argv: Sequence[str] | None) -> Iterator[None]:
    previous_program = os.environ.get("OPENALADDIN_CLI_PROG")
    previous_argv = sys.argv
    os.environ["OPENALADDIN_CLI_PROG"] = program
    if argv is not None:
        sys.argv = [program, *[str(value) for value in argv]]
    try:
        yield
    finally:
        if previous_program is None:
            os.environ.pop("OPENALADDIN_CLI_PROG", None)
        else:
            os.environ["OPENALADDIN_CLI_PROG"] = previous_program
        sys.argv = previous_argv


def _run_legacy(program: str, argv: Sequence[str] | None) -> int:
    legacy_frontend = _legacy_frontend()
    with _legacy_invocation(program, argv):
        namespace: dict[str, Any] = runpy.run_path(
            str(legacy_frontend),
            run_name="genie._legacy_oa",
        )
        return int(namespace["main"]())


def main(argv: Sequence[str] | None = None, *, program: str = "genie") -> int:
    """Run Genie, preserving the existing OA command surface for now."""

    return _run_legacy(program, argv)
