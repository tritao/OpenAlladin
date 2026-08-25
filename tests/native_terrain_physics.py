#!/usr/bin/env python3
"""Small deterministic regression for the fixed-ROM terrain slice."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import os


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/re/tests/native-terrain-physics/state.jsonl"


def main() -> int:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "110",
        "--state-output",
        str(OUTPUT),
        "--checkpoint-player",
        "103,416,0,0,1",
        "--input-schedule",
        "none*30,right*30,right+c,none*50",
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

    assert states[1]["terrain"]["behavior"] == 0x11
    assert states[31]["terrain"]["query_result"] == 0x77
    assert states[31]["terrain"]["push_right"] == 0xFF
    assert states[61]["player"]["vy"] == -0x200
    assert states[69]["player"]["vy"] == -0x20
    assert states[70]["player"]["vy"] == 0
    assert states[71]["player"]["vy"] == 0x3C
    assert any(
        record["player"]["grounded"]
        and record["player"]["y"] == 416
        and record["terrain"]["behavior"] == 0x11
        for record in states.values()
    )
    print("native terrain physics: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
