"""Workspace-aware helpers for invoking Genie and MAME tools."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
from typing import Iterable

def _workspace(root: Path | None = None) -> Path:
    if root is not None:
        return Path(root).resolve()
    from genie.context import ProjectContext

    return ProjectContext.discover().root


def tool_path(name: str, *, root: Path | None = None) -> Path:
    """Resolve a tool relative to the repository's ``genie`` directory."""

    return _workspace(root) / "genie" / name


def run_tool(
    name: str,
    args: Iterable[str] = (),
    *,
    env: dict[str, str] | None = None,
    root: Path | None = None,
) -> int:
    """Run a Python Genie tool and return its process status."""

    workspace = _workspace(root)
    command = [sys.executable, str(tool_path(name, root=workspace)), *map(str, args)]
    environment = dict(env or os.environ)
    python_path = environment.get("PYTHONPATH")
    environment["PYTHONPATH"] = str(workspace) + (
        os.pathsep + python_path if python_path else ""
    )
    return subprocess.run(command, cwd=workspace, env=environment, check=False).returncode


def run_shell_tool(
    name: str,
    args: Iterable[str] = (),
    *,
    env: dict[str, str] | None = None,
    root: Path | None = None,
) -> int:
    """Run an executable Genie/MAME tool and return its process status."""

    workspace = _workspace(root)
    command = [str(tool_path(name, root=workspace)), *map(str, args)]
    return subprocess.run(command, cwd=workspace, env=env, check=False).returncode


__all__ = ["run_shell_tool", "run_tool", "tool_path"]
