#!/usr/bin/env python3
"""Replay and compare a captured dynamic actor timeline."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "build/re/actor-flags-final/state.jsonl"
TIMELINE = ROOT / "re/actors/level01-interaction.timeline.tsv"
OUTPUT = ROOT / "build/re/tests/native-actor-timeline/state.jsonl"
SOURCE_START = 1436
FRAME_COUNT = 14


def load_states(path: Path) -> dict[int, dict]:
    states = {}
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state":
                states[int(record["frame"])] = record
    return states


def main() -> int:
    source = load_states(SOURCE)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        str(FRAME_COUNT),
        "--state-output",
        str(OUTPUT),
        "--actor-timeline",
        str(TIMELINE),
        "--checkpoint-player",
        "148,416,0,0,1",
        "--checkpoint-camera",
        "528,464,528,464,0,0,1",
        "--input-schedule",
        f"right*{FRAME_COUNT}",
    ]
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True)

    native = load_states(OUTPUT)
    for relative in range(FRAME_COUNT + 1):
        expected = source[SOURCE_START + relative]["actors"]
        actual = native[relative]["actors"]
        assert actual == expected, f"actor divergence at relative frame {relative}"

    slot4 = next(actor for actor in native[1]["actors"] if actor["slot"] == 4)
    assert slot4["flags"] == 0x20
    assert slot4["flag_bit5"] is True
    assert next(actor for actor in native[3]["actors"] if actor["slot"] == 0)["x"] == 682
    assert any(actor["slot"] == 10 for actor in native[1]["actors"])
    assert not any(
        actor["slot"] == 4 and actor["flags"] & 0x20
        for actor in native[13]["actors"]
    )

    print("native actor timeline: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
