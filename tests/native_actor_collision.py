#!/usr/bin/env python3
"""Regression for the fixed-ROM guard collision/death transition."""

from __future__ import annotations

import json
from pathlib import Path
import os
import subprocess


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/re/tests/native-actor-collision/state.jsonl"


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
        "--checkpoint-camera",
        "1000,416,1000,416,0,0,1",
        "--input-schedule",
        "a*1,none*45",
    ]
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True)

    states = {}
    with OUTPUT.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state":
                states[record["frame"]] = record

    first = states[1]
    guard = next(actor for actor in first["actors"] if actor["slot"] == 5)
    assert first["input"] == "a"
    assert first["player"]["attack_active"] is True
    assert first["player"]["animation_pc"] == 0x0012271A
    assert guard["type"] == 0x84
    assert guard["animation_pc"] == 0x00122FA2
    assert guard["terminal_timer"] == 43

    terminal = states[43]
    assert next(actor for actor in terminal["actors"] if actor["slot"] == 5)["type"] == 0x84
    assert not any(actor["slot"] == 5 for actor in states[44]["actors"])

    print("native actor collision: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
