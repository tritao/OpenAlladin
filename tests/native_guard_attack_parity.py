#!/usr/bin/env python3
"""Compare the live player sword/guard kill transition in MAME and native."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "rom/Disneys_Aladdin_U_p1.bin"
MAME_RUNNER = ROOT / "genie/mame/run.sh"
BINARY = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/openaladdin"

BOOT = "none*320,start*5,none*200,start*5,none*170,start*5,none*200,start*5,none*150,start*5,none*180"
MAME_INPUT = f"{BOOT},right*390,b*2,none*70"
NATIVE_INPUT = "none*1,right*390,b*2,none*107"


def load_states(path: Path) -> list[dict]:
    result = []
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state":
                result.append(record)
    return result


def actor(record: dict, slot: int) -> dict | None:
    return next(
        (item for item in record.get("actors", []) if item.get("slot") == slot),
        None,
    )


def first_attack(states: list[dict]) -> dict:
    return next(record for record in states if record.get("input") == "b")


def first_guard_hit(states: list[dict]) -> tuple[int, int, dict]:
    previous: dict[int, int] = {}
    for record in states:
        for item in record.get("actors", []):
            slot = int(item["slot"])
            actor_type = int(item.get("type", 0))
            if previous.get(slot) == 0x0A and actor_type == 0x84:
                return slot, int(record["frame"]), item
            previous[slot] = actor_type
    raise AssertionError("the live guard never transitioned from 0x0A to 0x84")


def retirement_frame(states: list[dict], slot: int, hit_frame: int) -> int:
    for record in states:
        if int(record["frame"]) <= hit_frame:
            continue
        item = actor(record, slot)
        if item is None or (
            int(item.get("type", 0)) == 0 and int(item.get("flags", 0)) == 0
        ):
            return int(record["frame"])
    raise AssertionError(f"guard slot {slot} was not retired after frame {hit_frame}")


def assert_terminal(record: dict, label: str) -> None:
    assert int(record["type"]) == 0x84, f"{label}: expected terminal guard"
    assert int(record["animation_pc"]) == 0x00122FA2, (
        f"{label}: unexpected death animation {record['animation_pc']:#x}"
    )
    assert int(record["frame_ptr"]) == 0, f"{label}: hit frame still has a sprite"
    assert record.get("collision_box") is None, f"{label}: hit guard still collides"


def run_mame(directory: Path) -> list[dict]:
    environment = dict(os.environ)
    environment.update(
        {
            "OPENALADDIN_CAPTURE": "state",
            "OPENALADDIN_CAPTURE_VDP": "0",
            "OPENALADDIN_TRACE_ACTORS": "1",
            "OPENALADDIN_TRACE_FRAMES": "1730",
            "OPENALADDIN_TRACE_DIR": str(directory),
            "OPENALADDIN_INPUT": MAME_INPUT,
            "OPENALADDIN_MAME_HEADLESS": "1",
            "OPENALADDIN_MAME_VIDEO": "none",
            "OPENALADDIN_MAME_SOUND": "none",
        }
    )
    result = subprocess.run(
        [str(MAME_RUNNER), str(ROM)],
        cwd=ROOT,
        env=environment,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    assert result.returncode == 0, result.stdout
    return load_states(directory / "state.jsonl")


def run_native(path: Path) -> list[dict]:
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    result = subprocess.run(
        [
            str(BINARY),
            "--no-window",
            "--no-audio",
            "--rom",
            str(ROM),
            "--frames",
            "500",
            "--state-output",
            str(path),
            "--input-schedule",
            NATIVE_INPUT,
        ],
        cwd=ROOT,
        env=environment,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    assert result.returncode == 0, result.stdout
    return load_states(path)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="openaladdin-guard-attack-parity-") as directory:
        root = Path(directory)
        mame_states = run_mame(root / "mame")
        native_states = run_native(root / "native.jsonl")

    mame_attack = first_attack(mame_states)
    native_attack = first_attack(native_states)
    mame_slot, mame_hit, mame_guard = first_guard_hit(mame_states)
    native_slot, native_hit, native_guard = first_guard_hit(native_states)

    # The two clients publish their state boundary on opposite sides of the
    # animation/collision service, so the observable hit may differ by one
    # state record. The semantic transition and terminal lifetime must agree.
    mame_attack_to_hit = mame_hit - int(mame_attack["frame"])
    native_attack_to_hit = native_hit - int(native_attack["frame"])
    assert abs(mame_attack_to_hit - native_attack_to_hit) <= 1, (
        f"guard hit timing diverged: MAME={mame_attack_to_hit}, "
        f"native={native_attack_to_hit}"
    )
    assert_terminal(mame_guard, "MAME")
    assert_terminal(native_guard, "native")

    mame_retired = retirement_frame(mame_states, mame_slot, mame_hit)
    native_retired = retirement_frame(native_states, native_slot, native_hit)
    assert mame_retired - mame_hit == 43, "MAME guard lifetime changed"
    assert native_retired - native_hit == 43, "native guard lifetime changed"

    print(
        "guard attack parity: ok "
        f"(MAME slot {mame_slot}, hit +{mame_attack_to_hit}; "
        f"native slot {native_slot}, hit +{native_attack_to_hit})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
