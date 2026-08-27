"""Shared provenance records for generated reverse-engineering artifacts."""

from __future__ import annotations

from datetime import datetime, timezone
import json
from pathlib import Path
from typing import Any

from . import __version__
from .context import ProjectContext


PROVENANCE_FORMAT = "genie-provenance-v1"


def _generated_at() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _installed_ghidra_version(context: ProjectContext) -> str:
    properties = context.ghidra_install_dir / "Ghidra/application.properties"
    if properties.is_file():
        for line in properties.read_text(encoding="utf-8").splitlines():
            if line.startswith("application.version="):
                return line.partition("=")[2].strip()
    return context.ghidra_version


def build_provenance(
    context: ProjectContext | None = None,
    *,
    generated_at: str | None = None,
    tool_version: str | None = None,
    strict: bool = True,
) -> dict[str, str]:
    """Build the standard provenance object for an artifact.

    A missing ROM is an error by default: generated RE data should not be
    silently detached from a ROM identity.  ``strict=False`` is useful for
    workspace diagnostics and packaging smoke tests.
    """

    context = context or ProjectContext.discover()
    identity = context.rom_identity
    if identity is None and strict:
        raise FileNotFoundError(f"ROM not found: {context.rom_path}")
    return {
        "rom_sha256": str((identity or {}).get("sha256", "")),
        "repository_commit": context.repository_commit,
        "mame_commit": context.mame_commit,
        "ghidra_version": _installed_ghidra_version(context),
        "tool_version": tool_version or __version__,
        "generated_at": generated_at or _generated_at(),
    }


def attach_provenance(
    document: dict[str, Any],
    context: ProjectContext | None = None,
    *,
    generated_at: str | None = None,
    strict: bool = True,
) -> dict[str, Any]:
    """Return *document* with a standard ``provenance`` member attached."""

    result = dict(document)
    result["provenance"] = build_provenance(
        context,
        generated_at=generated_at,
        strict=strict,
    )
    return result


def write_provenance_json(
    path: Path,
    document: dict[str, Any],
    context: ProjectContext | None = None,
    *,
    generated_at: str | None = None,
    strict: bool = True,
) -> None:
    """Write a JSON artifact with provenance attached."""

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(
            attach_provenance(document, context, generated_at=generated_at, strict=strict),
            indent=2,
            sort_keys=True,
        ) + "\n",
        encoding="utf-8",
    )


# ``create_provenance`` is a readable compatibility alias for service code.
create_provenance = build_provenance
