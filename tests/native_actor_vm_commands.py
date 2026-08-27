#!/usr/bin/env python3
"""Differentially exercise a controlled ROM movement command stream."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
FIXTURE_BINARY = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/native_actor_vm_fixture"
SOURCE_START = 361
FRAME_COUNT = 20
SLOT = 31
MOVEMENT_FIELDS = (
    "x",
    "y",
    "movement_pc",
    "movement_loop_pc",
    "movement_loop_timer",
    "movement_command_timer",
)
FACING_FIELDS = ("facing_x_flip", "facing_y_flip")


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


def project(actor: dict, include_facing: bool) -> dict:
    fields = MOVEMENT_FIELDS + (FACING_FIELDS if include_facing else ())
    return {field: actor.get(field, 0) for field in fields}


def capture_mame(
    trace: Path,
    movement_pc: str,
    x: int = 150,
    y: int = 416,
    return_pc: str | None = None,
) -> None:
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
            "OPENALADDIN_INJECT_ACTOR_X": str(x),
            "OPENALADDIN_INJECT_ACTOR_Y": str(y),
            "OPENALADDIN_INJECT_ACTOR_FACING_X": "0",
            "OPENALADDIN_INJECT_ACTOR_FACING_Y": "0",
            "OPENALADDIN_INJECT_ACTOR_FLAGS": "0",
            "OPENALADDIN_INJECT_ACTOR_MOVEMENT_TIMER": "0",
        }
    )
    environment.pop("OPENALADDIN_INJECT_ACTOR_RETURN_PC", None)
    if return_pc is not None:
        environment["OPENALADDIN_INJECT_ACTOR_RETURN_PC"] = return_pc
    subprocess.run(
        [str(ROOT / "genie/mame/run.sh")],
        cwd=ROOT,
        env=environment,
        check=True,
        stdout=subprocess.DEVNULL,
    )


def main() -> int:
    probes = (
        ("81", "0x11f730", 150, 416, None),
        ("82", "0x11f728", 150, 416, None),
        ("8d", "0x12171c", 150, 416, None),
        ("92-return", "0x1209ba", 150, 416, "0x1209be"),
        ("93-far", "0x120432", 150, 416, None),
        ("93-near", "0x120432", 32, 416, None),
        ("94-far", "0x1204ee", 150, 416, None),
    )
    for name, movement_pc, x, y, return_pc in probes:
        trace = ROOT / f"build/re/actor-vm-command-{name}"
        capture_mame(trace, movement_pc, x, y, return_pc)
        source = load_states(trace / "state.jsonl")

        native_output = trace / "native-state.jsonl"
        subprocess.run(
            [
                str(FIXTURE_BINARY),
                "--rom",
                str(ROOT / "rom/Disneys_Aladdin_U_p1.bin"),
                "--frames",
                str(FRAME_COUNT),
                "--state-output",
                str(native_output),
                "--actor-records",
                str(ROOT / f"re/actors/actor-vm-command-{name}.tsv"),
            ],
            cwd=ROOT,
            env={**os.environ, "SDL_VIDEODRIVER": "dummy"},
            check=True,
            stdout=subprocess.DEVNULL,
        )
        native = load_states(native_output)

        for relative in range(FRAME_COUNT + 1):
            include_facing = name in {"81", "8d"}
            expected = project(actor_at(source, SOURCE_START + relative), include_facing)
            actual = project(actor_at(native, relative), include_facing)
            assert actual == expected, (
                f"movement command {name} divergence at relative frame {relative}: "
                f"native={actual} mame={expected}"
            )

    print("native actor VM commands: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
