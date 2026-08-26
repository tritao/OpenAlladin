#!/usr/bin/env python3
"""Smoke-test the native player animation VM and Chopper frame database."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def run_case(name: str, checkpoint: str, schedule: str, frames: int = 2) -> list[dict]:
    output = ROOT / "build/re/tests/native-sprites" / f"{name}.jsonl"
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        str(frames),
        "--state-output",
        str(output),
        "--checkpoint-player",
        checkpoint,
        "--input-schedule",
        schedule,
    ]
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True, stdout=subprocess.DEVNULL)
    with output.open(encoding="utf-8") as stream:
        return [
            record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        ]


def main() -> int:
    idle = run_case("idle", "103,416,0,0,1", "none*2")
    assert idle[0]["player"]["sprite_frame"] == 201

    running = run_case("run", "103,416,0,0,1", "right*7", frames=7)
    run_frames = [record["player"]["sprite_frame"] for record in running]
    assert run_frames[:5] == [201, 201, 201, 201, 201]
    assert 202 in run_frames
    assert running[-1]["player"]["animation_state"] == "run"

    reversal = run_case("reversal", "103,416,0,0,1", "left*4,right*4", frames=8)
    assert [record["player"]["facing_x_flip"] for record in reversal] == [
        0, 255, 255, 255, 255, 0, 0, 0, 0
    ]

    jumping = run_case("jump", "103,416,0,-512,0", "none*7", frames=7)
    jump_frames = [record["player"]["sprite_frame"] for record in jumping]
    assert jump_frames[:5] == [161, 161, 161, 161, 162]
    assert jumping[-1]["player"]["sprite_frame"] == 162
    assert jumping[-1]["player"]["animation_state"] == "jump"

    print("native sprites: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
