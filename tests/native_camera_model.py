#!/usr/bin/env python3
"""Smoke-test the fixed-ROM camera follower and its trace fields."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/re/tests/native-camera-model/state.jsonl"


def main() -> int:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "41",
        "--state-output",
        str(OUTPUT),
        "--checkpoint-player",
        "87,416,0,0,1",
        "--checkpoint-camera",
        "16,464,16,464,0,0,1",
        "--input-schedule",
        "right*30,c,none*10",
    ]
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True, stdout=subprocess.DEVNULL)

    states = {}
    with OUTPUT.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state":
                states[record["frame"]] = record

    assert states[0]["player"]["world_x"] == 103
    assert states[0]["camera"]["pixel_x"] == 103
    assert states[0]["camera"]["tile_x"] == 0
    assert states[1]["camera"]["horizontal_threshold"] == 112
    assert states[1]["camera"]["update_delay"] == 6
    assert states[13]["camera"]["x"] == 17
    assert states[24]["camera"]["reference_x"] == 32
    assert states[24]["camera"]["scroll_x"] == 0
    assert states[25]["camera"]["x"] == 34
    assert states[26]["camera"]["x"] == 36
    assert states[26]["player"]["x"] == 145
    assert states[30]["camera"]["x"] == 45
    assert states[31]["camera"]["horizontal_threshold"] == 176
    assert states[31]["camera"]["vertical_threshold"] == 368
    assert states[38]["camera"]["x"] == 44
    assert states[38]["camera"]["y"] == 469
    assert states[40]["camera"]["x"] == 41
    assert states[40]["camera"]["y"] == 476
    print("native camera model: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
