#!/usr/bin/env python3
"""Tests for normalized scheduler comparison and first-divergence output."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
COMPARATOR = ROOT / "genie/mame/compare_scheduler.py"


def write_trace(path: Path, records: list[dict]) -> None:
    path.write_text(
        "\n".join(json.dumps(record, separators=(",", ":")) for record in records) + "\n",
        encoding="utf-8",
    )


def scheduler_trace() -> list[dict]:
    return [
        {"type": "header", "format": "openaladdin-scheduler-trace-v1"},
        {
            "type": "frame",
            "frame": 1,
            "input": "right",
            "phases": [
                {"name": "frame_loop", "rom_entry_pc": 1},
                {"name": "terrain_response", "rom_entry_pc": 2},
                {"name": "player_integrate", "rom_entry_pc": 3},
                {"name": "state_boundary", "rom_entry_pc": 4},
            ],
            "writer_pcs": [99],
        },
        {
            "type": "frame",
            "frame": 2,
            "input": "none",
            "phases": [
                {"name": "frame_loop", "rom_entry_pc": 1},
                {"name": "terrain_response", "rom_entry_pc": 2},
                {"name": "player_integrate", "rom_entry_pc": 3},
                {"name": "state_boundary", "rom_entry_pc": 4},
            ],
            "writer_pcs": [],
        },
    ]


def state_trace(*, second_terrain_phase: str = "terrain_input") -> list[dict]:
    first = {
        "type": "state",
        "frame": 1,
        "input": "right",
        "causal": {
            "phase_order": ["frame_latch", second_terrain_phase, "player_movement", "state_boundary"],
            "phase_pcs": [1, 2, 3, 4],
            "writer_pcs": [99],
        },
    }
    second = {
        "type": "state",
        "frame": 2,
        "input": "none",
        "causal": {
            "phase_order": ["frame_latch", second_terrain_phase, "player_movement", "state_boundary"],
            "phase_pcs": [1, 2, 3, 4],
            "writer_pcs": [],
        },
    }
    return [
        {"type": "header", "format": "openaladdin-frame-state-v3"},
        first,
        second,
    ]


def run(*arguments: str) -> subprocess.CompletedProcess[str]:
    environment = dict(os.environ)
    environment["PYTHONPATH"] = str(ROOT)
    return subprocess.run(
        [sys.executable, str(COMPARATOR), *arguments],
        cwd=ROOT,
        env=environment,
        capture_output=True,
        text=True,
        check=False,
    )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="openaladdin-scheduler-compare-") as name:
        directory = Path(name)
        genesis = directory / "genesis.jsonl"
        native = directory / "native.jsonl"
        write_trace(genesis, scheduler_trace())
        write_trace(native, state_trace())

        result = run(str(genesis), str(native), "--include-pcs", "--include-writers")
        assert result.returncode == 0, result.stdout + result.stderr
        assert "Scheduler traces match for 2 frame(s)" in result.stdout

        mismatch = directory / "mismatch.jsonl"
        write_trace(mismatch, state_trace(second_terrain_phase="terrain_contour"))
        result = run(str(genesis), str(mismatch))
        assert result.returncode == 1, result.stdout + result.stderr
        assert "First scheduler divergence: frame 1" in result.stdout
        assert "phase_names[1]" in result.stdout
        assert "Last matching scheduler frame: none" in result.stdout
        assert "Genesis writer PCs observed" in result.stdout

        projected = run(
            str(genesis),
            str(mismatch),
            "--phase",
            "frame_loop",
            "--phase",
            "player_integrate",
        )
        assert projected.returncode == 0, projected.stdout + projected.stderr
        assert "phase projection: frame_loop, player_integrate" in projected.stdout

    print("scheduler comparator: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
