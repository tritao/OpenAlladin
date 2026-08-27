#!/usr/bin/env python3
"""Regression corpus for the native recovered frame scheduler trace."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/openaladdin"


REQUIRED_ORDER = (
    "frame_latch",
    "input_resource",
    "publish_player_world_coordinates",
    "terrain_contour",
    "publish_player_world_coordinates",
    "movement_vm",
    "actor_terrain_collision",
    "player_actor_interaction",
    "terrain_resolution",
    "player_movement",
    "publish_player_world_coordinates",
    "camera_follow",
    "actor_collision",
    "level_exit_transition",
    "empty_return",
    "interaction_counter",
    "interaction_resource",
    "publish_player_world_coordinates",
    "scene_advance",
    "animation_vm",
    "transition_completion",
    "camera_scroll_publish",
    "scene_completion",
    "state_boundary",
)

EXPECTED_PCS = {
    "frame_latch": 0x001A8C16,
    "input_resource": 0x001A91C6,
    "publish_player_world_coordinates": 0x001A8E0C,
    "movement_vm": 0x001ADE36,
    "actor_terrain_collision": 0x001ADB5C,
    "player_actor_interaction": 0x001ABB40,
    "actor_collision": 0x001ABD7E,
    "level_exit_transition": 0x001A8F0C,
    "empty_return": 0x001A8F04,
    "animation_vm": 0x001AC784,
    "player_movement": 0x001A9D98,
    "terrain_resolution": 0x001B1E38,
    "camera_follow": 0x001AA90C,
    "interaction_counter": 0x001B00CA,
    "interaction_resource": 0x001B01AC,
    "scene_advance": 0x001A8E3E,
    "transition_completion": 0x001AE0F6,
    "camera_scroll_publish": 0x001AAA2A,
    "scene_completion": 0x001B315C,
}

EXPECTED_PHASE_SEQUENCE = (
    "frame_latch",
    "input_resource",
    "publish_player_world_coordinates",
    "terrain_contour",
    "publish_player_world_coordinates",
    "movement_vm",
    "actor_terrain_collision",
    "player_actor_interaction",
    "terrain_resolution",
    "player_movement",
    "publish_player_world_coordinates",
    "camera_follow",
    "actor_collision",
    "level_exit_transition",
    "empty_return",
    "interaction_counter",
    "interaction_resource",
    "publish_player_world_coordinates",
    "scene_advance",
    "animation_vm",
    "transition_completion",
    "camera_scroll_publish",
    "scene_completion",
    "state_boundary",
)


def records(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]


def check_trace(
    scheduler_path: Path,
    state_path: Path,
    frame_count: int,
    route: str,
) -> bool:
    scheduler = records(scheduler_path)
    state = records(state_path)
    assert scheduler[0]["type"] == "header"
    assert scheduler[0]["format"] == "openaladdin-scheduler-trace-v1"
    frames = scheduler[1:]
    assert [record["frame"] for record in frames] == list(range(1, frame_count + 1))

    for record in frames:
        phases = record["phases"]
        names = [phase["name"] for phase in phases]
        assert tuple(names) == EXPECTED_PHASE_SEQUENCE, names
        assert names.count("animation_vm") == 1, names
        assert "probe_animation" not in names, names
        assert "actor_animation" not in names, names
        assert "player_animation" not in names, names
        positions = [names.index(name) for name in dict.fromkeys(REQUIRED_ORDER)]
        assert positions == sorted(positions), names
        assert isinstance(record["writer_pcs"], list)
        if "animation_spawn" in names:
            assert names.index("animation_spawn") < names.index("state_boundary")
        for phase in phases:
            if phase["name"] in EXPECTED_PCS:
                assert phase["rom_entry_pc"] == EXPECTED_PCS[phase["name"]]

    state_frames = [record for record in state if record.get("type") == "state"]
    assert [record["frame"] for record in state_frames] == list(range(0, frame_count + 1))
    for scheduler_record, state_record in zip(frames, state_frames[1:]):
        causal = state_record["causal"]
        assert causal["phase_order"] == [phase["name"] for phase in scheduler_record["phases"]]
        assert causal["phase_pcs"] == [phase["rom_entry_pc"] for phase in scheduler_record["phases"]]
        assert causal["writer_pcs"] == scheduler_record["writer_pcs"]
    if route == "jump":
        assert any(record["player"]["vy"] < 0 for record in state_frames)
    elif route == "sword":
        assert any(record["player"]["attack_active"] for record in state_frames)
    elif route == "apple":
        assert any(
            actor.get("spawned_by_apple")
            for record in state_frames
            for actor in record["actors"]
        )
    return any(record["writer_pcs"] for record in frames)


def run_case(
    directory: Path,
    schedule: str,
    suffix: str,
    frame_count: int,
) -> tuple[Path, Path]:
    scheduler = directory / f"{suffix}.scheduler.jsonl"
    state = directory / f"{suffix}.state.jsonl"
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    result = subprocess.run(
        [
            str(BINARY),
            "--no-window",
            "--no-audio",
            "--frames",
            str(frame_count),
            "--input-schedule",
            schedule,
            "--scheduler-trace",
            str(scheduler),
            "--state-output",
            str(state),
        ],
        cwd=ROOT,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    return scheduler, state


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="openaladdin-native-scheduler-") as name:
        directory = Path(name)
        writer_cases = []
        cases = {
            "idle": ("none*8", 8),
            "run": ("right*12", 12),
            "jump": ("right*30,jump*1,none*96", 127),
            "sword": ("right*30,a*2,none*96", 128),
            "apple": ("right*30,apple*1,none*128", 159),
        }
        for route, (schedule, frame_count) in cases.items():
            scheduler, state = run_case(directory, schedule, route, frame_count)
            writer_cases.append(check_trace(scheduler, state, frame_count, route))

        first_scheduler, _ = run_case(directory, "right*12", "repeat-a", 12)
        second_scheduler, _ = run_case(directory, "right*12", "repeat-b", 12)
        assert first_scheduler.read_bytes() == second_scheduler.read_bytes()
        assert any(writer_cases)

    print("native scheduler trace: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
