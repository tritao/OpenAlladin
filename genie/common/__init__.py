"""Shared helpers exposed through the canonical Genie namespace."""

from __future__ import annotations

from typing import Any

from .helpers import (
    ROOT,
    hashes,
    load_yaml,
    normalize_symbols,
    normalize_types,
    parse_int,
    rom_entries,
    write_json,
    write_mame_symbols,
)

__all__ = [
    "ROOT",
    "ProjectContext",
    "PROVENANCE_FORMAT",
    "attach_provenance",
    "build_provenance",
    "find_repository_root",
    "git_revision",
    "hashes",
    "load_yaml",
    "normalize_symbols",
    "normalize_types",
    "parse_int",
    "rom_entries",
    "write_json",
    "write_mame_symbols",
    "write_provenance_json",
]


def __getattr__(name: str) -> Any:
    """Load context compatibility exports only after this package initializes.

    ``genie.context`` imports ``genie.common.helpers``.  Importing the context
    symbols eagerly from this package therefore creates a cycle while Python
    is still initializing ``genie.common``.  Keep the established import
    surface lazy so callers can still use ``from genie.common import
    ProjectContext`` without coupling package initialization to context.
    """

    if name in {"ProjectContext", "find_repository_root", "git_revision"}:
        from genie import context

        return getattr(context, name)
    if name in {
        "PROVENANCE_FORMAT",
        "attach_provenance",
        "build_provenance",
        "write_provenance_json",
    }:
        from genie import provenance

        return getattr(provenance, name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
