#!/usr/bin/env python3
"""Differentially exercise a controlled ROM movement command stream."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SOURCE_START = 361
FRAME_COUNT = 20
SLOT = 31
FIELDS = (
    "x",
    "y",
    "facing_x_flip",
    "facing_y_flip",
    "movement_pc",
    "movement_loop_pc",
    "movement_loop_timer",
    "movement_command_timer",
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


def capture_mame(trace: Path, movement_pc: str) -> None:
    trace.mkdir(parents=True, exist_ok=True)
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
            "OPENALADDIN_INJECT_ACTOR_MOVEMENT_PC": movement_pc,
            "OPENALADDIN_INJECT_ACTOR_X": "150",
            "OPENALADDIN_INJECT_ACTOR_Y": "416",
            "OPENALADDIN_INJECT_ACTOR_FACING_X": "0",
            "OPENALADDIN_INJECT_ACTOR_FACING_Y": "0",
            "OPENALADDIN_INJECT_ACTOR_FLAGS": "0",
            "OPENALADDIN_INJECT_ACTOR_MOVEMENT_TIMER": "0",
        }
    )
    subprocess.run(
        [str(ROOT / "tools/openaladdin/mame/run.sh")],
        cwd=ROOT,
        env=environment,
        check=True,
        stdout=subprocess.DEVNULL,
    )


def main() -> int:
    probes = (
        ("81", "0x11f730"),
        ("82", "0x11f728"),
        ("8d", "0x12171c"),
    )
    for name, movement_pc in probes:
        trace = ROOT / f"build/re/actor-vm-command-{name}"
        capture_mame(trace, movement_pc)
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
                str(ROOT / f"re/actors/actor-vm-command-{name}.tsv"),
                "--checkpoint-player",
                "0,0,0,0,0",
                "--checkpoint-camera",
                "0,0,0,0,0,0,1",
                "--input-schedule",
                f"none*{FRAME_COUNT}",
            ],
            cwd=ROOT,
            env={**os.environ, "SDL_VIDEODRIVER": "dummy"},
            check=True,
            stdout=subprocess.DEVNULL,
        )
        native = load_states(native_output)

        for relative in range(FRAME_COUNT + 1):
            expected = project(actor_at(source, SOURCE_START + relative))
            actual = project(actor_at(native, relative))
            assert actual == expected, (
                f"movement command {name} divergence at relative frame {relative}: "
                f"native={actual} mame={expected}"
            )

    print("native actor VM commands: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
