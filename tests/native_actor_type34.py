#!/usr/bin/env python3
"""Regression for the caller-specific Level-01 type-0x34 actor state."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
ACTORS = ROOT / "re/actors/actor-type34-reuse.tsv"
OUTPUT = ROOT / "build/re/tests/native-actor-type34/state.jsonl"


def load_states(path: Path) -> dict[int, dict]:
    result: dict[int, dict] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        record = json.loads(line)
        if record.get("type") == "state":
            result[int(record["frame"])] = record
    return result


def actor_at(state: dict, slot: int) -> dict:
    return next(actor for actor in state["actors"] if actor["slot"] == slot)


def main() -> int:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            str(ROOT / "build/openaladdin"),
            "--no-window",
            "--no-audio",
            "--frames",
            "1",
            "--state-output",
            str(OUTPUT),
            "--actor-records",
            str(ACTORS),
            "--checkpoint-player",
            "100,416,0,0,1",
            "--checkpoint-camera",
            "1000,464,1000,464,0,0,1,-1,-1,-1",
            "--input-schedule",
            "none*1",
        ],
        cwd=ROOT,
        env={**os.environ, "SDL_VIDEODRIVER": "dummy"},
        check=True,
    )

    states = load_states(OUTPUT)
    initial = actor_at(states[0], 8)
    first_tick = actor_at(states[1], 8)

    assert initial["type"] == 0x34
    assert initial["x"] == 1648
    assert initial["y"] == 656
    assert initial["movement_pc"] == 0x001217B4
    assert initial["animation_pc"] == 0x00122C1E

    assert first_tick["type"] == 0x34
    assert first_tick["x"] == 1648
    assert first_tick["y"] == 657
    assert first_tick["movement_pc"] == 0x001217B8
    assert first_tick["movement_command_timer"] == 3
    assert first_tick["animation_pc"] == 0x00122C22
    assert first_tick["frame_ptr"] == 0x001F3DEE
    assert first_tick["animation_timer"] == 9

    print("native actor type 0x34 reuse: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
