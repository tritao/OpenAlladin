#!/usr/bin/env python3
"""Small deterministic regression for the fixed-ROM terrain slice."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import os


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/re/tests/native-terrain-physics/state.jsonl"
SPECIAL_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/special-state.jsonl"
STOP_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/stop-state.jsonl"


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

    # These checkpoints select fixed level-01 cells that the generic opening
    # room route does not visit. They protect the real handler-table indices,
    # including the 0x47 surface-mode handler that was previously unreachable
    # in the native slice because its table was truncated/misaligned.
    special_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(SPECIAL_OUTPUT),
        "--checkpoint-player",
        "160,408,0,0,1",
        "--checkpoint-camera",
        "2096,328,16,464,0,0,1",
    ]
    subprocess.run(special_command, cwd=ROOT, env=environment, check=True)
    with SPECIAL_OUTPUT.open(encoding="utf-8") as stream:
        special_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    special = special_states[1]
    assert special["terrain"]["behavior"] == 0x47
    assert special["terrain"]["surface_mode"] == 1
    assert special["terrain"]["surface_latch"] == 0xFF

    stop_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(STOP_OUTPUT),
        "--checkpoint-player",
        "162,408,0,180,0",
        "--checkpoint-camera",
        "2740,312,16,464,0,0,1",
    ]
    subprocess.run(stop_command, cwd=ROOT, env=environment, check=True)
    with STOP_OUTPUT.open(encoding="utf-8") as stream:
        stop_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    stop = stop_states[1]
    assert stop["terrain"]["behavior"] == 0x2B
    assert stop["player"]["vx"] == 0
    assert stop["player"]["vy"] > 0

    print("native terrain physics: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
