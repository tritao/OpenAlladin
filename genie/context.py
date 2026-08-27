"""Canonical OpenAladdin project and workspace context."""

from __future__ import annotations

from dataclasses import dataclass
import os
import subprocess
from pathlib import Path
from typing import Any

from openaladdin.common import hashes, load_yaml


ROM_IDENTITY_FIELDS = ("size", "crc32", "sha1", "sha256")


def find_repository_root(start: Path | None = None) -> Path:
    """Find the repository containing the tracked ROM configuration."""

    configured_root = os.environ.get("OPENALADDIN_ROOT") if start is None else None
    candidate = Path(configured_root or start or Path(__file__)).resolve()
    if candidate.is_file():
        candidate = candidate.parent
    for directory in (candidate, *candidate.parents):
        if (directory / "re/config/roms.yml").is_file() and (directory / "tools").is_dir():
            return directory
    return candidate


def git_revision(path: Path) -> str:
    """Return the revision for *path*, or an empty string if unavailable."""

    try:
        result = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            cwd=path if path.is_dir() else None,
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return ""
    return result.stdout.strip() if result.returncode == 0 else ""


def _config(root: Path, relative_path: str) -> dict[str, Any]:
    value = load_yaml(root / relative_path) or {}
    return value if isinstance(value, dict) else {}


@dataclass(frozen=True, slots=True)
class ProjectContext:
    """All workspace paths and revisions used by Genie services."""

    repository_root: Path
    rom_path: Path
    rom_entry_name: str
    rom_entry: dict[str, Any]
    build_dir: Path
    re_dir: Path
    mame_path: Path
    ghidra_install_dir: Path
    ghidra_project_dir: Path
    ghidra_project_name: str
    ghidra_version: str
    native_executable: Path
    repository_commit: str
    mame_commit: str

    @classmethod
    def discover(
        cls,
        root: Path | None = None,
        rom: Path | None = None,
    ) -> "ProjectContext":
        repository_root = find_repository_root(root).resolve()
        rom_config = _config(repository_root, "re/config/roms.yml")
        default_name = str(rom_config.get("default", "aladdin_local"))
        entries = {
            str(key): value
            for key, value in rom_config.items()
            if key != "default" and isinstance(value, dict)
        }
        rom_entry = dict(entries.get(default_name, {}))
        configured_rom = Path(str(rom_entry.get("expected_filename", "rom/Disneys_Aladdin_U_p1.bin")))
        rom_path = Path(rom) if rom is not None else configured_rom
        if not rom_path.is_absolute():
            rom_path = repository_root / rom_path

        ghidra_config = _config(repository_root, "re/config/ghidra.yml")
        ghidra = ghidra_config.get("ghidra") or {}
        mame_path = repository_root / "external/mame/mame"
        return cls(
            repository_root=repository_root,
            rom_path=rom_path.resolve(),
            rom_entry_name=default_name,
            rom_entry=rom_entry,
            build_dir=repository_root / "build",
            re_dir=repository_root / "re",
            mame_path=mame_path,
            ghidra_install_dir=repository_root / str(ghidra.get("install_dir", ".tools/ghidra")),
            ghidra_project_dir=repository_root / str(ghidra.get("project_dir", "re/ghidra/project")),
            ghidra_project_name=str(ghidra.get("project_name", "aladdin")),
            ghidra_version=str(ghidra.get("version", "")),
            native_executable=repository_root / "build/openaladdin",
            repository_commit=git_revision(repository_root),
            mame_commit=git_revision(repository_root / "external/mame"),
        )

    # Short aliases make the context pleasant to use in service code while
    # the explicit names remain the serialized/canonical vocabulary.
    @property
    def root(self) -> Path:
        return self.repository_root

    @property
    def rom(self) -> Path:
        return self.rom_path

    @property
    def mame(self) -> Path:
        return self.mame_path

    @property
    def ghidra(self) -> Path:
        return self.ghidra_install_dir

    @property
    def rom_identity(self) -> dict[str, Any] | None:
        return hashes(self.rom_path) if self.rom_path.is_file() else None

    @property
    def expected_rom_identity(self) -> dict[str, Any]:
        return {
            key: self.rom_entry[key]
            for key in ROM_IDENTITY_FIELDS
            if key in self.rom_entry
        }

    @property
    def rom_matches(self) -> bool:
        actual = self.rom_identity
        expected = self.expected_rom_identity
        if actual is None or not expected:
            return False
        return all(str(actual[key]).upper() == str(expected[key]).upper() for key in expected)

    def as_dict(self) -> dict[str, Any]:
        """Return a JSON-ready workspace description."""

        return {
            "repository_root": str(self.repository_root),
            "rom_path": str(self.rom_path),
            "rom_entry": self.rom_entry_name,
            "build_dir": str(self.build_dir),
            "re_dir": str(self.re_dir),
            "mame_path": str(self.mame_path),
            "ghidra_install_dir": str(self.ghidra_install_dir),
            "ghidra_project_dir": str(self.ghidra_project_dir),
            "ghidra_project_name": self.ghidra_project_name,
            "native_executable": str(self.native_executable),
            "repository_commit": self.repository_commit,
            "mame_commit": self.mame_commit,
        }
