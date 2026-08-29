#!/usr/bin/env python3
"""Regression tests for synchronization-qualified actor parity traces."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
COMPARE_STATE = ROOT / "genie/core/mame/trace.py"
COMPARE_ACTORS = ROOT / "genie/games/aladdin/mame/compare_actors.py"


ATOMIC_FIELDS = ["player", "camera", "terrain", "scene", "actors"]


def write_trace(path: Path, header: dict, records: list[dict]) -> None:
    path.write_text(
        "\n".join(json.dumps(record) for record in [header, *records]) + "\n",
        encoding="utf-8",
    )


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False)


def main() -> int:
    header = {
        "type": "header",
        "format": "openaladdin-frame-state-v2",
        "rom_sha256": "fixture",
        "sync": {
            "boundary": "VBlankInterrupt",
            "state_boundary": "game-loop",
            "atomic_fields": ATOMIC_FIELDS,
            "actors_qualified": True,
        },
    }
    actor = {
        "slot": 0,
        "type": 0x7D,
        "x": 100,
        "y": 200,
        "movement_flags": 0,
        "sprite_attribute": 0,
        "facing_x_flip": 0,
        "facing_y_flip": 0,
        "frame_ptr": 0,
        "animation_pc": 0x1234,
        "movement_pc": 0,
        "movement_loop_pc": 0,
        "movement_loop_timer": 0,
        "movement_word_18": 0,
        "movement_word_1a": 0,
        "animation_timer": 0,
        "movement_return_pc": 0,
        "flags": 0,
        "movement_command_timer": 0,
        "collision_box": None,
    }
    atomic = {
        "type": "state",
        "format": "openaladdin-frame-state-v2",
        "frame": 1,
        "capture": {"boundary": "game-loop", "atomic": True, "atomic_fields": ATOMIC_FIELDS},
        "player": {"x": 100},
        "camera": {"x": 0},
        "terrain": {"state": 0},
        "scene": {"state": 1},
        "actors": [actor] + [
            {"slot": slot, "type": 0, "flags": 0}
            for slot in range(1, 32)
        ],
    }
    raw = {
        "type": "state",
        "format": "openaladdin-frame-state-v2",
        "frame": 0,
        "capture": {"boundary": "video-frame-done", "atomic": False, "atomic_fields": []},
        "actors": [],
    }

    with tempfile.TemporaryDirectory(prefix="openaladdin-atomic-test-") as name:
        directory = Path(name)
        left = directory / "left.jsonl"
        right = directory / "right.jsonl"
        write_trace(left, header, [raw, atomic])
        write_trace(right, header, [raw, atomic])

        result = run([
            sys.executable,
            str(COMPARE_STATE),
            str(left),
            str(right),
            "--require-atomic",
            "--atomic-only",
        ])
        assert result.returncode == 0, result.stderr or result.stdout
        assert "Traces match for 1 frame(s)." in result.stdout

        v3_header = {
            "type": "header",
            "format": "openaladdin-frame-state-v3",
            "rom_sha256": "fixture",
            "sync": {
                "boundary": "VBlankInterrupt",
                "state_boundary": "game-loop",
                "atomic_fields": [*ATOMIC_FIELDS, "scheduler"],
                "atomic_actor_fields": ["type", "x", "animation_timer"],
                "actors_qualified": True,
                "actor_slot_count": 32,
            },
        }
        v3 = directory / "v3.jsonl"
        write_trace(
            v3,
            v3_header,
            [{
                **atomic,
                "format": "openaladdin-frame-state-v3",
                "capture": {
                    "boundary": "game-loop",
                    "atomic": True,
                    "atomic_fields": [*ATOMIC_FIELDS, "scheduler"],
                    "atomic_actor_fields": ["type", "x", "animation_timer"],
                },
                "scheduler": {"random_state": 7},
            }],
        )
        result = run([
            sys.executable,
            str(COMPARE_STATE),
            str(v3),
            str(v3),
            "--require-atomic",
            "--atomic-only",
        ])
        assert result.returncode == 0, result.stderr or result.stdout

        v3_left = directory / "v3-left.jsonl"
        v3_right = directory / "v3-right.jsonl"

        def v3_state(frame: int, random_state: int, input_value: str) -> dict:
            return {
                **atomic,
                "format": "openaladdin-frame-state-v3",
                "frame": frame,
                "input": input_value,
                "capture": {
                    "boundary": "game-loop",
                    "atomic": True,
                    "atomic_fields": [*ATOMIC_FIELDS, "scheduler"],
                    "atomic_actor_fields": ["type", "x", "animation_timer"],
                },
                "scheduler": {"random_state": random_state},
                "causal": {"writer_pcs": [0x1234 + frame]},
            }

        write_trace(v3_left, v3_header, [
            v3_state(1, 7, "right"),
            v3_state(2, 8, "none"),
        ])
        right_frame_two = v3_state(2, 9, "none")
        right_frame_two["causal"] = {
            "phase_order": ["native-only-detail"],
            "writer_pcs": [0xDEAD],
        }
        write_trace(v3_right, v3_header, [
            v3_state(1, 7, "right"),
            right_frame_two,
        ])
        result = run([
            sys.executable,
            str(COMPARE_STATE),
            str(v3_left),
            str(v3_right),
            "--require-atomic",
            "--atomic-only",
        ])
        assert result.returncode != 0
        assert "First divergence: frame 2" in result.stdout
        assert "Last matching state: S[1]" in result.stdout
        assert "Input: I[1] Genesis=right OpenAladdin=right" in result.stdout
        assert "Genesis changes since S[1]" in result.stdout
        assert "OpenAladdin changes since S[1]" in result.stdout
        assert "writer PCs observed" in result.stdout

        result = run([
            sys.executable,
            str(COMPARE_STATE),
            str(left),
            str(left),
            "--require-left-atomic",
            "--left-atomic-only",
        ])
        assert result.returncode == 0, result.stderr or result.stdout

        result = run([
            sys.executable,
            str(COMPARE_STATE),
            str(directory / "missing.jsonl"),
            str(right),
            "--require-left-atomic",
        ])
        assert result.returncode != 0

        unqualified = directory / "unqualified.jsonl"
        write_trace(
            unqualified,
            {"type": "header", "format": "openaladdin-frame-state-v1"},
            [raw],
        )
        result = run([
            sys.executable,
            str(COMPARE_STATE),
            str(unqualified),
            str(unqualified),
            "--require-left-atomic",
        ])
        assert result.returncode != 0
        assert "not actor-qualified" in result.stderr

        native = directory / "native.jsonl"
        native_header = {"type": "header", "format": "openaladdin-frame-state-v1", "rom_sha256": "fixture"}
        native_actor = dict(actor)
        write_trace(native, native_header, [{**atomic, "actors": [native_actor]}])
        result = run([
            sys.executable,
            str(COMPARE_ACTORS),
            str(left),
            str(native),
            "--require-left-atomic",
            "--left-atomic-only",
            "--include-player",
        ])
        assert result.returncode == 0, result.stderr or result.stdout

    print("atomic trace parity: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
