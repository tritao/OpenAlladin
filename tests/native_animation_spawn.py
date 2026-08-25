#!/usr/bin/env python3
"""Regression for the ROM animation VM F5 actor-spawn request."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/re/tests/native-animation-spawn/state.jsonl"


def load_states(path: Path) -> dict[int, dict]:
    states = {}
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state":
                states[int(record["frame"])] = record
    return states


def main() -> int:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "1",
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
    assert not any(actor["slot"] >= 25 for actor in states[0]["actors"])
    spawned = next(actor for actor in states[1]["actors"] if actor["slot"] == 25)
    assert spawned["type"] == 0x80
    assert spawned["x"] == 1143
    assert spawned["y"] == 844
    assert spawned["movement_pc"] == 0x0011F6D4
    assert spawned["animation_pc"] == 0x00122B58
    assert spawned["frame_ptr"] == 0
    assert spawned["flags"] == 0x08

    print("native animation F5 spawn: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
