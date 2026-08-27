#!/usr/bin/env python3
"""Regression for the ROM animation VM F5 actor-spawn request."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/re/tests/native-animation-spawn/state.jsonl"
PLAYER_OUTPUT = ROOT / "build/re/tests/native-animation-spawn/player-state.jsonl"


def load_states(path: Path) -> dict[int, dict]:
    states = {}
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state":
                states[int(record["frame"])] = record
    return states


def active_slots(state: dict) -> set[int]:
    return {
        item["slot"]
        for item in state["actors"]
        if item.get("type", 0) != 0 or item.get("flags", 0) != 0
    }


def main() -> int:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(OUTPUT),
        "--actor-records",
        str(ROOT / "re/actors/level01.tsv"),
        "--checkpoint-player",
        "103,416,0,0,1",
        "--checkpoint-camera",
        "1000,464,1000,464,0,0,1",
        # 0x00122438 is the captured player action stream containing:
        # F5 03 00 1B 79 18 28 DC ...
        "--checkpoint-animation",
        "0x00122438,0",
        "--input-schedule",
        "none*1",
    ]
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True)

    states = load_states(OUTPUT)
    assert not any(slot >= 25 for slot in active_slots(states[0]))
    spawned = next(actor for actor in states[1]["actors"] if actor["slot"] == 25)
    assert spawned["type"] == 0x80
    assert spawned["x"] == 1143
    assert spawned["y"] == 844
    assert spawned["movement_pc"] == 0x0011F6D4
    assert spawned["animation_pc"] == 0x00122B58
    assert spawned["frame_ptr"] == 0
    assert spawned["flags"] == 0x08

    moved = next(actor for actor in states[2]["actors"] if actor["slot"] == 25)
    assert moved["x"] == 1143
    assert moved["y"] == 844
    assert moved["movement_pc"] == 0x0011F6DE
    assert moved["movement_loop_pc"] == 0x0011F6D8
    assert moved["movement_loop_timer"] == 0x2C
    assert moved["movement_word_1a"] == 0x006E

    # Mode 0 is the common actor allocation path used by the opening Level 01
    # player stream. It is a nested F5 service of ordinal 30, so the new
    # record is allocated before that ordinal's single actor-table traversal.
    player_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "3",
        "--state-output",
        str(PLAYER_OUTPUT),
        "--checkpoint-player",
        "87,416,0,0,1",
        "--checkpoint-camera",
        "16,464,16,464,0,0,1",
        "--checkpoint-animation",
        "0x00122046,0",
        "--input-schedule",
        "none*3",
    ]
    subprocess.run(player_command, cwd=ROOT, env=environment, check=True)
    player_states = load_states(PLAYER_OUTPUT)
    player_spawn = next(actor for actor in player_states[1]["actors"] if actor["slot"] == 3)
    assert player_spawn["type"] == 0x84
    assert player_spawn["x"] == 99
    assert player_spawn["y"] == 895
    assert player_spawn["animation_pc"] == 0x001245D0
    assert player_spawn["frame_ptr"] != 0

    print("native animation F5 spawn: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
