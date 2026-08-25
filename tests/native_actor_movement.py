#!/usr/bin/env python3
"""Compare the native actor movement VM with a captured MAME stream."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "build/re/actor-flags-final/state.jsonl"
ACTORS = ROOT / "re/actors/actor-movement.tsv"
OUTPUT = ROOT / "build/re/tests/native-actor-movement/state.jsonl"
SOURCE_START = 361
FRAME_COUNT = 20
ACTOR_FIELDS = (
    "slot",
    "type",
    "x",
    "y",
    "movement_pc",
    "frame_ptr",
    "animation_pc",
    "flags",
)


def load_states(path: Path) -> dict[int, dict]:
    states = {}
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state":
                states[int(record["frame"])] = record
    return states


def project(actor: dict) -> dict:
    return {field: actor.get(field, 0) for field in ACTOR_FIELDS}


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
        "--actor-records",
        str(ACTORS),
        "--checkpoint-player",
        "103,416,0,0,1",
        "--checkpoint-camera",
        "528,464,528,464,0,0,1",
        "--input-schedule",
        f"none*{FRAME_COUNT}",
    ]
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True)

    native = load_states(OUTPUT)
    for relative in range(FRAME_COUNT + 1):
        expected_actor = next(
            actor
            for actor in source[SOURCE_START + relative]["actors"]
            if actor["slot"] == 19
        )
        actual_actor = next(
            actor for actor in native[relative]["actors"] if actor["slot"] == 19
        )
        assert project(actual_actor) == project(expected_actor), (
            f"actor movement divergence at relative frame {relative}: "
            f"native={project(actual_actor)} expected={project(expected_actor)}"
        )

    # The final captured frame has executed the 0x84 AF command at 0x12038E.
    # Its timer is intentionally exposed even though the old MAME trace did
    # not serialize this RAM byte yet.
    final_actor = next(actor for actor in native[FRAME_COUNT]["actors"] if actor["slot"] == 19)
    assert final_actor["movement_command_timer"] == 0x2F
    assert final_actor["movement_pc"] == 0x120390

    print("native actor movement: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
