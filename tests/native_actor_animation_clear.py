#!/usr/bin/env python3
"""Regression for the common actor VM's EC 01 animation-state clear."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/re/tests/native-actor-animation-clear/state.jsonl"
ACTORS = ROOT / "re/actors/actor-animation-clear.tsv"


def load_states(path: Path) -> dict[int, dict]:
    result: dict[int, dict] = {}
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state":
                result[int(record["frame"])] = record
    return result


def main() -> int:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    command = [
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
        "560,816,0,0,1",
        "--checkpoint-camera",
        "400,464,400,464,0,0,1,-1,-1,-1",
        "--input-schedule",
        "none*1",
    ]
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True)

    states = load_states(OUTPUT)
    before = next(actor for actor in states[0]["actors"] if actor["slot"] == 8)
    after = next(actor for actor in states[1]["actors"] if actor["slot"] == 8)

    assert before["type"] == 0x40
    assert before["animation_pc"] == 0x00122C12
    assert after["type"] == 0x40
    assert after["animation_pc"] == 0
    assert after["frame_ptr"] == 0x001F84A4
    assert after["x"] == 560
    assert after["y"] == 816

    print("native actor EC 01 animation clear: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
