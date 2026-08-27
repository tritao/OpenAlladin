#!/usr/bin/env python3
"""Verify native startup configuration for each ordinary extracted level."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from genie.common import load_yaml, parse_int


ORDINARY_LEVELS = tuple(range(8)) + tuple(range(9, 13))


def main() -> int:
    executable = Path(sys.argv[1])
    metadata = {
        int(row["index"]): row
        for row in (load_yaml(ROOT / "re/assets/level_table.yml") or {}).get("levels", [])
    }
    with tempfile.TemporaryDirectory(prefix="openaladdin-level-loader-") as directory:
        for index in ORDINARY_LEVELS:
            state_path = Path(directory) / f"level{index:02d}.jsonl"
            result = subprocess.run(
                [
                    str(executable),
                    "--no-window",
                    "--no-audio",
                    "--frames",
                    "1",
                    "--level-index",
                    str(index),
                    "--state-output",
                    str(state_path),
                ],
                cwd=ROOT,
                env={**os.environ, "SDL_VIDEODRIVER": "dummy"},
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode != 0:
                print(f"level {index}: native loader failed: {result.stderr.strip()}")
                return 1
            records = [
                json.loads(line)
                for line in state_path.read_text(encoding="utf-8").splitlines()
                if line.startswith('{"type":"state"')
            ]
            initial = next(record for record in records if record["frame"] == 0)
            spec = metadata[index]
            expected_player = tuple(parse_int(value) for value in spec["player_start"])
            expected_camera = tuple(parse_int(value) for value in spec["camera_start"])
            expected_dimensions = tuple(
                parse_int(value) * 16 for value in spec["map_size_blocks"]
            )
            actual_player = (initial["player"]["x"], initial["player"]["y"])
            actual_camera = (initial["camera"]["x"], initial["camera"]["y"])
            actual_dimensions = (
                initial["camera"]["level_width"],
                initial["camera"]["level_height"],
            )
            if (
                initial["scene"]["state"] != index
                or actual_player != expected_player
                or actual_camera != expected_camera
                or actual_dimensions != expected_dimensions
            ):
                print(
                    f"level {index}: mismatch scene={initial['scene']['state']} "
                    f"player={actual_player} camera={actual_camera} "
                    f"dimensions={actual_dimensions}"
                )
                return 1
            print(f"level {index:02d}: native startup configuration OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
