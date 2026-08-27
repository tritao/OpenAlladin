"""Workspace diagnostics for the Genie reverse-engineering environment."""

from __future__ import annotations

from dataclasses import dataclass
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Any

from .context import ProjectContext


@dataclass(frozen=True, slots=True)
class Diagnostic:
    name: str
    status: str
    detail: str
    fatal: bool = False

    def as_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "status": self.status,
            "detail": self.detail,
            "fatal": self.fatal,
        }


def _java_version() -> str:
    java = shutil.which("java")
    if not java:
        return ""
    try:
        result = subprocess.run(
            [java, "-version"],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return ""
    output = result.stderr or result.stdout
    match = re.search(r'version "([^"]+)"', output)
    return match.group(1) if match else ""


def _java_major(version: str) -> int | None:
    if not version:
        return None
    first = version.split(".", 1)[0]
    if first == "1":
        parts = version.split(".")
        return int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else None
    return int(first) if first.isdigit() else None


def _check_python() -> Diagnostic:
    current = sys.version_info[:2]
    required = (3, 10)
    detail = f"Python {current[0]}.{current[1]} (requires >= {required[0]}.{required[1]})"
    return Diagnostic("python", "OK" if current >= required else "FATAL", detail, current < required)


def _check_rom(context: ProjectContext) -> Diagnostic:
    if not context.rom_path.is_file():
        return Diagnostic("ROM", "FATAL", f"missing: {context.rom_path}", True)
    actual = context.rom_identity or {}
    if not context.rom_matches:
        return Diagnostic(
            "ROM",
            "FATAL",
            f"identity mismatch: sha256={actual.get('sha256', '')}",
            True,
        )
    return Diagnostic("ROM", "OK", f"{context.rom_path} ({actual.get('sha256', '')})")


def _check_ghidra(context: ProjectContext) -> Diagnostic:
    launcher = context.ghidra_install_dir / "support" / (
        "pyghidraRun.bat" if os.name == "nt" else "pyghidraRun"
    )
    if not launcher.is_file():
        return Diagnostic(
            "Ghidra",
            "OPTIONAL",
            f"not installed: {launcher}",
        )
    properties = context.ghidra_install_dir / "Ghidra/application.properties"
    installed = "configured version"
    if properties.is_file():
        installed = "installed"
    return Diagnostic("Ghidra", "OK", f"{installed}: {context.ghidra_install_dir}")


def _check_ghidra_project(context: ProjectContext) -> Diagnostic:
    project = context.ghidra_project_dir / f"{context.ghidra_project_name}.gpr"
    if not project.is_file():
        return Diagnostic("Ghidra project", "OPTIONAL", f"missing: {project}")
    return Diagnostic("Ghidra project", "OK", str(project))


def _check_mame(context: ProjectContext) -> Diagnostic:
    if not context.mame_path.is_file() or not os.access(context.mame_path, os.X_OK):
        return Diagnostic("MAME", "OPTIONAL", f"binary missing: {context.mame_path}")
    revision = context.mame_commit or "revision unavailable"
    return Diagnostic("MAME", "OK", f"{context.mame_path} ({revision})")


def _check_native(context: ProjectContext) -> Diagnostic:
    if not context.native_executable.is_file() or not os.access(context.native_executable, os.X_OK):
        return Diagnostic("native build", "OPTIONAL", f"missing: {context.native_executable}")
    return Diagnostic("native build", "OK", str(context.native_executable))


def _check_python_modules() -> Diagnostic:
    # The repository has a standard-library YAML fallback.  PyYAML is useful,
    # but its absence must not make a fresh checkout unusable.
    if importlib.util.find_spec("yaml") is None:
        return Diagnostic("Python modules", "OK", "standard library plus YAML fallback")
    return Diagnostic("Python modules", "OK", "standard library and PyYAML available")


def _check_java(context: ProjectContext) -> Diagnostic:
    configured = None
    config_path = context.repository_root / "re/config/ghidra.yml"
    try:
        from genie.common.helpers import load_yaml

        config = load_yaml(config_path) or {}
        configured = int((config.get("java") or {}).get("major"))
    except (OSError, TypeError, ValueError):
        pass
    version = _java_version()
    if not version:
        return Diagnostic("JDK", "OPTIONAL", "java executable not found")
    if shutil.which("javac") is None:
        return Diagnostic("JDK", "OPTIONAL", f"Java {version}; javac executable not found")
    major = _java_major(version)
    if configured is not None and major is not None and major < configured:
        return Diagnostic("JDK", "OPTIONAL", f"Java {version}; Ghidra recommends {configured}")
    return Diagnostic("JDK", "OK", f"Java {version}")


def _check_git(context: ProjectContext) -> Diagnostic:
    if not context.repository_commit:
        return Diagnostic("Git provenance", "OPTIONAL", "repository revision unavailable")
    try:
        result = subprocess.run(
            ["git", "-C", str(context.repository_root), "status", "--porcelain"],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return Diagnostic("Git provenance", "OPTIONAL", "git status unavailable")
    if result.returncode != 0:
        return Diagnostic("Git provenance", "OPTIONAL", "git status failed")
    if result.stdout.strip():
        return Diagnostic("Git provenance", "WARN", f"working tree has changes ({context.repository_commit})")
    return Diagnostic("Git provenance", "OK", context.repository_commit)


def diagnostics(context: ProjectContext | None = None, *, strict: bool = False) -> list[Diagnostic]:
    """Collect diagnostics without printing or mutating the workspace."""

    context = context or ProjectContext.discover()
    checks = [
        _check_python(),
        _check_rom(context),
        _check_python_modules(),
        _check_java(context),
        _check_ghidra(context),
        _check_ghidra_project(context),
        _check_mame(context),
        _check_native(context),
        _check_git(context),
    ]
    if strict:
        checks = [
            Diagnostic(item.name, "FATAL", item.detail, True)
            if item.status == "OPTIONAL" else item
            for item in checks
        ]
    return checks


def run_doctor(args: Any, context: ProjectContext | None = None) -> int:
    """Print the doctor report and return non-zero only for fatal checks."""

    rom = getattr(args, "rom", None)
    context = context or ProjectContext.discover(rom=rom)
    checks = diagnostics(context, strict=bool(getattr(args, "strict", False)))
    if getattr(args, "json_output", False):
        print(json.dumps({"context": context.as_dict(), "checks": [item.as_dict() for item in checks]}, indent=2, sort_keys=True))
    else:
        print(f"Genie doctor: {context.repository_root}")
        for item in checks:
            print(f"{item.status:<10} {item.name:<18} {item.detail}")
    return 1 if any(item.fatal for item in checks) else 0
