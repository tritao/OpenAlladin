#!/usr/bin/env python3
"""Regression for the live apple counter and its native HUD rendering."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/openaladdin"
sys.path.insert(0, str(ROOT))

from genie.analysis.visual_diff import read_image


def run_case(
    directory: Path,
    name: str,
    frames: int,
    schedule: str,
) -> tuple[Path, Path]:
    state = directory / f"{name}.state.jsonl"
    framebuffer = directory / f"{name}.ppm"
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    result = subprocess.run(
        [
            str(BINARY),
            "--no-window",
            "--no-audio",
            "--frames",
            str(frames),
            "--input-schedule",
            schedule,
            "--state-output",
            str(state),
            "--framebuffer-out",
            str(framebuffer),
            "--framebuffer-frame",
            str(frames),
        ],
        cwd=ROOT,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    return state, framebuffer


def state_records(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]


def hud_crop(path: Path) -> bytes:
    width, height, pixels = read_image(path)
    assert (width, height) == (320, 224)
    x0, y0, crop_width, crop_height = 264, 188, 52, 28
    return b"".join(
        pixels[(y * width + x0) * 3 : (y * width + x0 + crop_width) * 3]
        for y in range(y0, y0 + crop_height)
    )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="openaladdin-apple-hud-") as name:
        directory = Path(name)
        initial_state, initial_framebuffer = run_case(
            directory, "initial", 1, "none*1"
        )
        after_state, after_framebuffer = run_case(
            directory, "after-throw", 159, "right*30,a*1,none*128"
        )

        initial_records = [
            record for record in state_records(initial_state)
            if record.get("type") == "state"
        ]
        after_records = [
            record for record in state_records(after_state)
            if record.get("type") == "state"
        ]
        assert initial_records[0]["inventory"]["apple_count"] == 10
        assert after_records[-1]["inventory"]["apple_count"] == 9
        assert hud_crop(initial_framebuffer) != hud_crop(after_framebuffer)

    print("native apple hud: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
