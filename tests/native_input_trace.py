#!/usr/bin/env python3
"""Validate the native input/timing trace used for live jump diagnosis."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/openaladdin"


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="openaladdin-input-trace-") as directory:
        trace_path = Path(directory) / "input.jsonl"
        result = subprocess.run(
            [
                str(BINARY),
                "--no-audio",
                "--frames",
                "38",
                "--input-schedule",
                "right*30,jump*3,none*5",
                "--input-trace",
                str(trace_path),
            ],
            cwd=ROOT,
            env={**os.environ, "SDL_VIDEODRIVER": "dummy"},
            check=False,
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, result.stderr
        records = [json.loads(line) for line in trace_path.read_text().splitlines()]

    assert records[0]["format"] == "openaladdin-input-trace-v1"
    frames = records[1:]
    assert [record["frame"] for record in frames] == list(range(1, 39))
    assert all(record["source"] == "schedule" for record in frames)
    assert frames[0]["frame_interval_us"] == -1
    assert all(record["frame_interval_us"] >= 0 for record in frames[1:])
    # This is intentionally a windowed-mode run with SDL's dummy video
    # driver.  It exercises the same interactive pacing branch used by the
    # real window and rejects a loop that advances several simulation frames
    # per display frame.
    assert min(record["frame_interval_us"] for record in frames[1:]) >= 12000
    assert all(record["events"] == [] for record in frames)

    # The schedule holds jump for three simulation frames, but it must still
    # produce one edge.  Frame 31 is the first state produced by the jump
    # token after the initial thirty right-input frames.
    assert frames[30]["frame"] == 31
    assert frames[30]["held"]["jump"] is True
    assert frames[30]["pressed"]["jump"] is True
    assert frames[31]["held"]["jump"] is True
    assert frames[31]["pressed"]["jump"] is False
    assert frames[32]["held"]["jump"] is True
    assert frames[32]["pressed"]["jump"] is False
    assert frames[33]["held"]["jump"] is False
    assert frames[33]["pressed"]["jump"] is False

    print("native input trace: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
