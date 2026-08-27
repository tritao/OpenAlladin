#!/usr/bin/env python3
"""MAME/native differential regression for the recovered sword actor slice."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "rom/Disneys_Aladdin_U_p1.bin"
RUNNER = ROOT / "genie/mame/run.sh"
BINARY = ROOT / "build/openaladdin"
# The injected record is visible at frame 361 before its first common actor
# service. Start the differential at frame 362, where the native checkpoint
# represents the same post-service ROM boundary without a synthetic cadence
# field.
SOURCE_START = 362
LIFECYCLE_FRAMES = 50
SLOT = 25

FIELDS = (
    "type",
    "x",
    "y",
    "movement_pc",
    "movement_loop_pc",
    "movement_loop_timer",
    "movement_word_18",
    "movement_word_1a",
    "animation_pc",
    "frame_ptr",
    "animation_timer",
    "flags",
)


def load_states(path: Path) -> dict[int, dict]:
    states: dict[int, dict] = {}
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state":
                states[int(record["frame"])] = record
    return states


def find_actor(state: dict, slot: int = SLOT) -> dict | None:
    return next((actor for actor in state.get("actors", []) if actor.get("slot") == slot), None)


def mame_environment(trace_dir: Path, **overrides: str) -> dict[str, str]:
    environment = dict(os.environ)
    environment.update(
        {
            "SDL_VIDEODRIVER": "dummy",
            "OPENALADDIN_MAME_HEADLESS": "1",
            "OPENALADDIN_MAME_VIDEO": "none",
            "OPENALADDIN_MAME_SOUND": "none",
            "OPENALADDIN_CAPTURE_VDP": "0",
            "OPENALADDIN_TRACE_ACTORS": "1",
            "OPENALADDIN_TRACE_DIR": str(trace_dir),
            "OPENALADDIN_INPUT": "none*320,start*5,none*86",
            "OPENALADDIN_INJECT_ACTOR_FRAME": "361",
            "OPENALADDIN_INJECT_ACTOR_SLOT": str(SLOT),
            "OPENALADDIN_INJECT_ACTOR_TYPE": "128",
            "OPENALADDIN_INJECT_ACTOR_TEMPLATE": "0x1B7918",
            "OPENALADDIN_INJECT_ACTOR_PC": "0x122B5A",
            "OPENALADDIN_INJECT_ACTOR_FRAME_PTR": "0x1FD478",
            "OPENALADDIN_INJECT_ACTOR_MOVEMENT_PC": "0x11F6D4",
            "OPENALADDIN_INJECT_ACTOR_X": "150",
            "OPENALADDIN_INJECT_ACTOR_Y": "416",
            "OPENALADDIN_INJECT_ACTOR_FACING_X": "0",
            "OPENALADDIN_INJECT_ACTOR_FACING_Y": "0",
            "OPENALADDIN_INJECT_ACTOR_FLAGS": "10",
        }
    )
    environment.update(overrides)
    return environment


def run_mame(trace_dir: Path, frame_limit: int, **overrides: str) -> dict[int, dict]:
    environment = mame_environment(trace_dir, OPENALADDIN_TRACE_FRAMES=str(frame_limit), **overrides)
    subprocess.run(
        [str(RUNNER), str(ROM)],
        cwd=ROOT,
        env=environment,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return load_states(trace_dir / "state.jsonl")


def run_native(output: Path, frames: int, records: Path) -> dict[int, dict]:
    command = [
        str(BINARY),
        "--no-window",
        "--rom",
        str(ROM),
        "--frames",
        str(frames),
        "--state-output",
        str(output),
        "--actor-records",
        str(records),
        "--checkpoint-player",
        "0,0,0,0,0",
        "--checkpoint-camera",
        "0,0,0,0,0,0,1",
        "--input-schedule",
        f"none*{frames}",
    ]
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True, stdout=subprocess.PIPE, text=True)
    return load_states(output)


def compare_lifecycle(mame: dict[int, dict], native: dict[int, dict]) -> None:
    for frame in range(LIFECYCLE_FRAMES):
        mame_actor = find_actor(mame[SOURCE_START + frame])
        native_actor = find_actor(native[frame])
        if mame_actor is None or native_actor is None:
            raise AssertionError(
                f"sword actor presence diverged at native frame {frame}: "
                f"mame={mame_actor is not None} native={native_actor is not None}"
            )
        for field in FIELDS:
            if mame_actor.get(field) != native_actor.get(field):
                raise AssertionError(
                    f"sword actor first divergence at native frame {frame} "
                    f"(MAME {SOURCE_START + frame}), field {field}: "
                    f"mame={mame_actor.get(field)!r} native={native_actor.get(field)!r}"
                )


def compare_cleanup(mame: dict[int, dict], native: dict[int, dict]) -> None:
    mame_actor = find_actor(mame[SOURCE_START + 1])
    native_actor = find_actor(native[1])
    mame_type = 0 if mame_actor is None else mame_actor.get("type", 0)
    native_type = 0 if native_actor is None else native_actor.get("type", 0)
    if mame_type != 0 or native_type != 0:
        raise AssertionError(
            f"sword 0x8C cleanup diverged: mame_type={mame_type} native_type={native_type}"
        )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="openaladdin-sword-lifecycle-") as directory:
        root = Path(directory)
        mame = run_mame(root / "lifecycle-mame", SOURCE_START + LIFECYCLE_FRAMES)
        native = run_native(root / "lifecycle-native.jsonl", LIFECYCLE_FRAMES, ROOT / "re/actors/sword-movement-lifecycle.tsv")
        compare_lifecycle(mame, native)

        cleanup_mame = run_mame(
            root / "cleanup-mame",
            SOURCE_START + 2,
            OPENALADDIN_INJECT_ACTOR_PC="0",
            OPENALADDIN_INJECT_ACTOR_FRAME_PTR="0x1FD478",
            OPENALADDIN_INJECT_ACTOR_MOVEMENT_PC="0x11F6E6",
            OPENALADDIN_INJECT_ACTOR_MOVEMENT_LOOP_PC="0x11F6D8",
            OPENALADDIN_INJECT_ACTOR_MOVEMENT_LOOP_TIMER="0",
        )
        cleanup_native = run_native(root / "cleanup-native.jsonl", 1, ROOT / "re/actors/sword-cleanup.tsv")
        compare_cleanup(cleanup_mame, cleanup_native)

    print("native sword lifecycle differential: ok (50 frames + 0x8C cleanup)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
