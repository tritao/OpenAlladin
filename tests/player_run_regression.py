#!/usr/bin/env python3
"""Differential regression for the Level 01 run/release scheduler handoff."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools/oa.py"),
            "regression",
            "player-run",
            "--trace-dir",
            str(ROOT / "build/re/tests/player-run-regression"),
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    output = result.stdout + result.stderr
    if result.returncode != 0:
        raise AssertionError(f"player-run differential failed:\n{output}")
    marker = "Traces match for "
    matching = [line for line in output.splitlines() if marker in line]
    if not matching:
        raise AssertionError(f"differential did not report a match:\n{output}")
    frames = int(matching[-1].split(marker, 1)[1].split()[0])
    if frames < 157:
        raise AssertionError(f"differential covered only {frames} frames:\n{output}")
    print(f"player run regression: ok ({frames} frames)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
