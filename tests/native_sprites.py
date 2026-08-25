#!/usr/bin/env python3
"""Smoke-test hard-selected Chopper player frames in the native runtime."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def run_case(name: str, checkpoint: str, schedule: str) -> list[dict]:
    output = ROOT / "build/re/tests/native-sprites" / f"{name}.jsonl"
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
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

    running = run_case("run", "103,416,0,0,1", "right*2")
    assert running[-1]["player"]["sprite_frame"] == 202

    jumping = run_case("jump", "103,416,0,-512,0", "none*2")
    assert jumping[-1]["player"]["sprite_frame"] == 161

    print("native sprites: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
