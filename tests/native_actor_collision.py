#!/usr/bin/env python3
"""Regression for the fixed-ROM guard collision/death transition."""

from __future__ import annotations

import json
from pathlib import Path
import os
import subprocess


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/re/tests/native-actor-collision/state.jsonl"


def load_states(path: Path) -> dict[int, dict]:
    states = {}
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state":
                states[record["frame"]] = record
    return states


def main() -> int:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "46",
        "--actor-records",
        str(ROOT / "re/actors/guard-collision.tsv"),
        "--state-output",
        str(OUTPUT),
        "--checkpoint-player",
        "328,416,0,0,1",
        "--checkpoint-frame-ptr",
        "0x001EA794",
        "--checkpoint-camera",
        "1000,416,1000,416,0,0,1",
        "--input-schedule",
        "a*1,none*45",
    ]
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True)

    states = load_states(OUTPUT)

    first = states[1]
    guard = next(actor for actor in first["actors"] if actor["slot"] == 5)
    assert first["input"] == "a"
    assert first["player"]["attack_active"] is True
    assert states[0]["player"]["frame_ptr"] == 0x001EA794
    assert states[0]["player"]["collision_box"] == {
        "left": 1444,
        "top": 930,
        "right": 1468,
        "bottom": 973,
    }
    assert first["player"]["frame_ptr"] != 0
    initial_guard = next(actor for actor in states[0]["actors"] if actor["slot"] == 5)
    assert initial_guard["frame_ptr"] == 0x001F6500
    assert initial_guard["collision_box"] == {
        "left": 1448,
        "top": 917,
        "right": 1462,
        "bottom": 974,
    }
    assert first["player"]["animation_pc"] == 0x0012271A
    assert guard["type"] == 0x84
    assert guard["collision_box"] is None
    assert guard["animation_pc"] == 0x00122FA2

    terminal = states[43]
    assert next(actor for actor in terminal["actors"] if actor["slot"] == 5)["type"] == 0x84
    assert not any(actor["slot"] == 5 for actor in states[44]["actors"])

    # The recovered records produce a deliberately asymmetric horizontal
    # overlap. With the player frame at 0x1EA794 and guard frame at 0x1F6500,
    # the exact half-open collision tests hit for world X 0x51C..0x541 and
    # miss immediately outside those bounds.
    for local_x, expected_type in ((307, 0x0A), (308, 0x84), (345, 0x84), (346, 0x0A)):
        boundary_output = OUTPUT.parent / f"boundary-{local_x}.jsonl"
        boundary_command = [
            str(ROOT / "build/openaladdin"),
            "--no-window",
            "--frames",
            "1",
            "--actor-records",
            str(ROOT / "re/actors/guard-collision.tsv"),
            "--state-output",
            str(boundary_output),
            "--checkpoint-player",
            f"{local_x},416,0,0,1",
            "--checkpoint-frame-ptr",
            "0x001EA794",
            "--checkpoint-camera",
            "1000,416,1000,416,0,0,1",
            "--input-schedule",
            "a*1",
        ]
        subprocess.run(boundary_command, cwd=ROOT, env=environment, check=True, stdout=subprocess.DEVNULL)
        boundary_states = load_states(boundary_output)
        boundary_initial = next(actor for actor in boundary_states[0]["actors"] if actor["slot"] == 5)
        boundary_guard = next(actor for actor in boundary_states[1]["actors"] if actor["slot"] == 5)
        assert boundary_initial["collision_box"] == initial_guard["collision_box"]
        assert boundary_guard["type"] == expected_type, (
            f"hitbox boundary mismatch at local X {local_x}: "
            f"got 0x{boundary_guard['type']:02X}, expected 0x{expected_type:02X}"
        )

    print("native actor collision: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
