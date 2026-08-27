"""Shared helpers exposed through the canonical Genie namespace."""

from __future__ import annotations

from openaladdin.common import (
    ROOT as LEGACY_ROOT,
    hashes,
    load_yaml,
    normalize_symbols,
    normalize_types,
    parse_int,
    rom_entries,
    write_json,
    write_mame_symbols,
)

from genie.context import ProjectContext, find_repository_root, git_revision
from genie.provenance import (
    PROVENANCE_FORMAT,
    attach_provenance,
    build_provenance,
    create_provenance,
    write_provenance_json,
)

# The old helper's ROOT remains available under its historical name.  It is
# the same repository root for a normal checkout; new code should use the
# explicit ProjectContext instead of resolving paths ad hoc.
ROOT = LEGACY_ROOT

__all__ = [
    "ROOT",
    "ProjectContext",
    "PROVENANCE_FORMAT",
    "attach_provenance",
    "build_provenance",
    "create_provenance",
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
