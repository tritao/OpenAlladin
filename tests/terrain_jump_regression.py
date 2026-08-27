#!/usr/bin/env python3
"""Differential regression for the timed terrain-response jump arc."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "genie",
            "regression",
            "terrain-jump-land",
            "--trace-dir",
            str(ROOT / "build/re/tests/terrain-jump-regression"),
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    output = result.stdout + result.stderr
    if result.returncode != 0:
        raise AssertionError(f"terrain-jump-land differential failed:\n{output}")
    marker = "Traces match for "
    matching = [line for line in output.splitlines() if marker in line]
    if not matching:
        raise AssertionError(f"differential did not report a match:\n{output}")
    frames = int(matching[-1].split(marker, 1)[1].split()[0])
    if frames < 257:
        raise AssertionError(f"differential covered only {frames} frames:\n{output}")
    print(f"terrain jump regression: ok ({frames} frames)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
