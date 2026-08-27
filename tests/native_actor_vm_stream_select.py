#!/usr/bin/env python3
"""Differentially exercise the confirmed actor 0x8E stream selector."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SOURCE_START = 361
FRAME_COUNT = 3
SLOT = 31
FIELDS = (
    "type",
    "x",
    "y",
    "movement_pc",
    "animation_pc",
    "frame_ptr",
    "animation_timer",
)


def load_states(path: Path) -> dict[int, dict]:
    states: dict[int, dict] = {}
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state":
                states[int(record["frame"])] = record
    return states


def actor_at(states: dict[int, dict], frame: int) -> dict:
    return next(actor for actor in states[frame]["actors"] if actor["slot"] == SLOT)


def project(actor: dict) -> dict:
    return {field: actor.get(field, 0) for field in FIELDS}


def capture_mame(trace: Path) -> None:
    environment = dict(os.environ)
    environment.update(
        {
            "OPENALADDIN_CAPTURE_VDP": "0",
            "OPENALADDIN_TRACE_ACTORS": "1",
            "OPENALADDIN_TRACE_FRAMES": str(SOURCE_START + FRAME_COUNT),
            "OPENALADDIN_TRACE_DIR": str(trace),
            "OPENALADDIN_INPUT": "none*320,start*5,none*55",
            "OPENALADDIN_INJECT_ACTOR_FRAME": str(SOURCE_START),
            "OPENALADDIN_INJECT_ACTOR_SLOT": str(SLOT),
            "OPENALADDIN_INJECT_ACTOR_TYPE": "125",
            "OPENALADDIN_INJECT_ACTOR_PC": "0x125952",
            "OPENALADDIN_INJECT_ACTOR_MOVEMENT_PC": "0x04bbd6",
            "OPENALADDIN_INJECT_ACTOR_X": "150",
            "OPENALADDIN_INJECT_ACTOR_Y": "416",
            "OPENALADDIN_INJECT_ACTOR_FACING_X": "0",
            "OPENALADDIN_INJECT_ACTOR_FACING_Y": "0",
            "OPENALADDIN_INJECT_ACTOR_FLAGS": "0",
            "OPENALADDIN_INJECT_ACTOR_MOVEMENT_TIMER": "0",
        }
    )
    subprocess.run(
        [str(ROOT / "genie/mame/run.sh")],
        cwd=ROOT,
        env=environment,
        check=True,
        stdout=subprocess.DEVNULL,
    )


def main() -> int:
    trace = ROOT / "build/re/actor-vm-command-8e"
    capture_mame(trace)
    source = load_states(trace / "state.jsonl")

    native_output = trace / "native-state.jsonl"
    subprocess.run(
        [
            str(ROOT / "build/openaladdin"),
            "--no-window",
            "--frames",
            str(FRAME_COUNT),
            "--state-output",
            str(native_output),
            "--actor-records",
            str(ROOT / "re/actors/actor-vm-command-8e.tsv"),
            "--checkpoint-player",
            "0,0,0,0,0",
            "--checkpoint-camera",
            "0,0,0,0,0,0,1",
            "--input-schedule",
            f"none*{FRAME_COUNT}",
        ],
        cwd=ROOT,
        check=True,
        stdout=subprocess.DEVNULL,
    )
    native = load_states(native_output)

    for relative in range(FRAME_COUNT + 1):
        expected = project(actor_at(source, SOURCE_START + relative))
        actual = project(actor_at(native, relative))
        assert actual == expected, (
            f"movement command 8e divergence at relative frame {relative}: "
            f"native={actual} mame={expected}"
        )

    print("native actor VM stream selection: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
