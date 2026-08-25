#!/usr/bin/env python3
"""Regression for the Genesis actor-to-actor sword/guard collision path."""

from __future__ import annotations

import json
from pathlib import Path
import os
import subprocess


ROOT = Path(__file__).resolve().parents[1]
ACTORS = ROOT / "re/actors/guard-sword-collision.tsv"
OUTPUT = ROOT / "build/re/tests/native-actor-actor-collision/state.jsonl"


def load_states(path: Path) -> dict[int, dict]:
    states = {}
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state":
                states[int(record["frame"])] = record
    return states


def actor(state: dict, slot: int) -> dict:
    return next(item for item in state["actors"] if item["slot"] == slot)


def main() -> int:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "44",
        "--state-output",
        str(OUTPUT),
        "--actor-records",
        str(ACTORS),
        "--checkpoint-player",
        "103,416,0,0,1",
        "--checkpoint-camera",
        "528,464,528,464,0,0,1",
        "--input-schedule",
        "none*44",
    ]
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True)

    states = load_states(OUTPUT)
    initial_guard = actor(states[0], 5)
    initial_sword = actor(states[0], 26)
    assert initial_guard["type"] == 0x0A
    assert initial_guard["facing_x_flip"] == 0xFF
    assert initial_guard["collision_box"] == {
        "left": 1450,
        "top": 917,
        "right": 1208,
        "bottom": 974,
    }
    assert initial_sword["type"] == 0x80
    assert initial_sword["collision_box"] == {
        "left": 1434,
        "top": 935,
        "right": 1448,
        "bottom": 949,
    }

    hit_guard = actor(states[1], 5)
    hit_sword = actor(states[1], 26)
    assert hit_guard["type"] == 0x84
    assert hit_guard["animation_pc"] == 0x00122FA2
    assert hit_guard["frame_ptr"] == 0
    assert hit_guard["terminal_timer"] == 43
    assert hit_sword["type"] == 0x84
    assert hit_sword["animation_pc"] == 0x00122DD8
    assert hit_sword["frame_ptr"] == 0
    assert hit_sword["terminal_timer"] == 19

    assert actor(states[43], 5)["type"] == 0x84
    assert 26 not in {item["slot"] for item in states[20]["actors"]}
    assert 5 not in {item["slot"] for item in states[44]["actors"]}

    print("native actor-to-actor collision: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
