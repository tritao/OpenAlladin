#!/usr/bin/env python3
"""Regression for a live sword strike against the opening guard."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/openaladdin"
OUTPUT = ROOT / "build/re/tests/native-first-guard-sword/state.jsonl"


def states(path: Path) -> list[dict]:
    with path.open(encoding="utf-8") as stream:
        return [
            record
            for line in stream
            if (record := json.loads(line)).get("type") == "state"
        ]


def main() -> int:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(BINARY),
        "--no-window",
        "--no-audio",
        "--rom",
        str(ROOT / "rom/Disneys_Aladdin_U_p1.bin"),
        "--frames",
        "425",
        "--state-output",
        str(OUTPUT),
        # The opening guard is reached by the same simple walk used for the
        # MAME guard probe. Press A once while stopped just before its box.
        "--input-schedule",
        "none*1,right*390,a*1,none*40",
    ]
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True, stdout=subprocess.DEVNULL)

    frame_states = states(OUTPUT)
    attack_frame = next(record for record in frame_states if record["input"] == "a")
    assert attack_frame["player"]["attack_timer"] == 10
    assert attack_frame["player"]["world_x"] == 1273
    attack_boundary = frame_states[attack_frame["frame"] + 1]
    assert attack_boundary["player"]["animation_pc"] == 0x0012271A

    previous_guard_type = {}
    guard_hit_frame = None
    for record in frame_states:
        guard = next(
            (actor for actor in record["actors"] if actor["slot"] == 4),
            None,
        )
        if guard is not None:
            if previous_guard_type.get(4) == 0x0A and guard["type"] == 0x84:
                guard_hit_frame = record["frame"]
                assert guard["animation_pc"] == 0x00122FA2
                break
            previous_guard_type[4] = guard["type"]
    assert guard_hit_frame is not None, "the opening guard was never hit"

    print("native first-guard sword: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
