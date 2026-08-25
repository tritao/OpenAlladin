#!/usr/bin/env python3
"""Import a runtime coverage report into the local Ghidra project."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess

from openaladdin.common import ROOT, load_yaml


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "coverage",
        nargs="?",
        type=Path,
        default=ROOT / "build/re/coverage.json",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "build/re/coverage-ghidra.json",
    )
    parser.add_argument("--project-dir", type=Path)
    args = parser.parse_args()

    coverage = args.coverage.resolve()
    if not coverage.is_file():
        raise SystemExit(f"coverage report not found: {coverage}; run oa coverage merge")
    config = load_yaml(ROOT / "re/config/ghidra.yml")
    project_dir = (args.project_dir or ROOT / config["ghidra"]["project_dir"]).resolve()
    project_name = config["ghidra"]["project_name"]
    project_marker = project_dir / f"{project_name}.gpr"
    project_rep = project_dir / f"{project_name}.rep"
    if not project_marker.is_file() and not project_rep.is_dir():
        raise SystemExit(f"Ghidra project not found: {project_dir}; run oa ghidra rebuild first")

    headless = ROOT / config["ghidra"]["install_dir"] / "support/pyghidraRun"
    if os.name == "nt":
        headless = headless.with_suffix(".bat")
    if not headless.is_file():
        raise SystemExit(f"Ghidra launcher not found: {headless}; run oa setup first")

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(headless),
        "-H",
        str(project_dir),
        project_name,
        "-process",
        "-noanalysis",
        "-scriptPath",
        str(ROOT / "re/ghidra/scripts"),
        "-postScript",
        "ImportRuntimeCoverage.py",
        str(coverage),
        str(output),
    ]
    environment = os.environ.copy()
    environment["GHIDRA_INSTALL_DIR"] = str(ROOT / config["ghidra"]["install_dir"])
    environment["OPENALADDIN_ROOT"] = str(ROOT)
    venv_bin = ROOT / ".tools/venv/bin"
    if venv_bin.is_dir():
        environment["PATH"] = str(venv_bin) + os.pathsep + environment.get("PATH", "")
    print("Importing runtime coverage into Ghidra")
    subprocess.run(command, cwd=ROOT, env=environment, check=True)
    if not output.is_file():
        raise SystemExit(f"Ghidra did not produce the import report: {output}")
    print(f"report: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
