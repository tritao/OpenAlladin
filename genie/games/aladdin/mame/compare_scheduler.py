#!/usr/bin/env python3
"""Compare normalized frame-scheduler traces.

The native scheduler trace and the MAME state trace deliberately expose
different amounts of implementation detail.  This tool normalizes names
without pretending that the two traces have identical phase granularity.
Use ``--phase`` to compare an explicit shared projection when that is the
right question; the default compares the complete normalized sequence and
reports the first causal mismatch.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


SCHEDULER_FORMAT = "openaladdin-scheduler-trace-v1"
STATE_FORMATS = {
    "openaladdin-frame-state-v1",
    "openaladdin-frame-state-v2",
    "openaladdin-frame-state-v3",
}


# These aliases describe semantic phase families, not an assertion that the
# source implementations run the same number of subphases.  In particular,
# pre/post native collision passes remain distinct unless the caller selects
# a projection that explicitly aliases them.
PHASE_ALIASES = {
    "frame_latch": "frame_loop",
    "frame_loop": "frame_loop",
    "input_resource": "input_resource",
    "deferred_animation_spawn": "animation_spawn",
    "animation_spawn": "animation_spawn",
    "terrain_input": "terrain_response",
    "terrain_response": "terrain_response",
    "terrain_contour": "terrain_contour",
    "publish_player_world_coordinates": "publish_player_world_coordinates",
    "terrain_resolution": "terrain_resolve",
    "terrain_resolve": "terrain_resolve",
    "pre_motion_actor_collision": "actor_collision_pre",
    "post_motion_actor_collision": "actor_collision_post",
    "actor_collision": "actor_collision",
    "actor_terrain_collision": "actor_terrain",
    "actor_culling": "actor_culling",
    "probe_animation": "animation_probe",
    "movement_vm": "movement_vm",
    "actor_terrain": "actor_terrain",
    "actor_terrain_interaction": "actor_terrain_interaction",
    "player_actor_interaction": "player_interaction",
    "player_interaction": "player_interaction",
    "player_collision": "player_collision",
    "interaction_refill": "interaction_refill",
    "interaction_counter": "interaction_counter",
    "interaction_resource": "interaction_resource",
    "interaction_rows_a": "interaction_rows_a",
    "interaction_rows_b": "interaction_rows_b",
    "camera_reference_rebase": "camera_reference",
    "camera_follow": "camera_follow",
    "camera_scroll_publish": "camera_scroll_publish",
    "player_movement": "player_integrate",
    "player_integrate": "player_integrate",
    "actor_animation": "actor_animation",
    "player_animation": "player_animation",
    "scene_advance": "scene_advance",
    "transition_completion": "transition_completion",
    "scene_completion": "scene_completion",
    "level_exit_transition": "level_exit_transition",
    "empty_return": "empty_return",
    "actor_initialize": "actor_initialize",
    "level01_gate": "level_gate",
    "state_boundary": "state_boundary",
}


class TraceError(SystemExit):
    """A user-facing trace format error."""


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise TraceError(f"{path}: cannot read trace: {error}") from error
    for line_number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise TraceError(f"{path}:{line_number}: invalid JSON: {error}") from error
        if not isinstance(record, dict):
            raise TraceError(f"{path}:{line_number}: trace record is not an object")
        records.append(record)
    if not records:
        raise TraceError(f"{path}: trace is empty")
    return records


def _integer_list(value: Any, path: Path, label: str) -> list[int]:
    if value is None:
        return []
    if not isinstance(value, list):
        raise TraceError(f"{path}: {label} must be a list")
    try:
        return [int(item) for item in value]
    except (TypeError, ValueError) as error:
        raise TraceError(f"{path}: {label} contains a non-integer") from error


def _phase_names(value: Any, path: Path) -> list[str]:
    if value is None:
        return []
    if not isinstance(value, list):
        raise TraceError(f"{path}: phase_order must be a list")
    result: list[str] = []
    for item in value:
        if isinstance(item, dict):
            name = item.get("name")
        else:
            name = item
        if name is None:
            raise TraceError(f"{path}: scheduler phase has no name")
        result.append(str(name))
    return result


def normalize_phase(name: str) -> str:
    """Return the stable semantic name for one recovered phase label."""
    return PHASE_ALIASES.get(name, name)


def _scheduler_record(
    record: dict[str, Any],
    path: Path,
    *,
    state_record: bool,
) -> dict[str, Any] | None:
    if record.get("type") != "state" and state_record:
        return None
    if record.get("type") != "frame" and not state_record:
        return None
    if "frame" not in record:
        raise TraceError(f"{path}: scheduler record has no frame")
    try:
        frame = int(record["frame"])
    except (TypeError, ValueError) as error:
        raise TraceError(f"{path}: scheduler frame is not an integer") from error

    if state_record:
        causal = record.get("causal")
        if not isinstance(causal, dict):
            return None
        names = _phase_names(causal.get("phase_order"), path)
        phase_pcs = _integer_list(causal.get("phase_pcs"), path, "phase_pcs")
        writers = _integer_list(causal.get("writer_pcs"), path, "writer_pcs")
        # Native state traces emit the initial pre-update state with an empty
        # causal object. It is not a scheduler boundary and must not create a
        # false frame-0 "missing record" divergence.
        if not names and not phase_pcs and not writers:
            return None
    else:
        names = _phase_names(record.get("phases"), path)
        phase_pcs = []
        phases = record.get("phases") or []
        for phase in phases:
            if not isinstance(phase, dict):
                raise TraceError(f"{path}: scheduler phase must be an object")
            if "rom_entry_pc" not in phase:
                raise TraceError(f"{path}: scheduler phase has no rom_entry_pc")
            try:
                phase_pcs.append(int(phase["rom_entry_pc"]))
            except (TypeError, ValueError) as error:
                raise TraceError(f"{path}: phase PC is not an integer") from error
        writers = _integer_list(record.get("writer_pcs"), path, "writer_pcs")

    if phase_pcs and len(phase_pcs) != len(names):
        raise TraceError(
            f"{path}: phase name/PC count differs at frame {frame}: "
            f"{len(names)} != {len(phase_pcs)}"
        )
    return {
        "frame": frame,
        "input": str(record.get("input", "none")),
        "phase_names": names,
        "phase_pcs": phase_pcs,
        "writer_pcs": writers,
    }


def load_trace(path: Path) -> dict[int, dict[str, Any]]:
    records = read_jsonl(path.resolve())
    header = next((record for record in records if record.get("type") == "header"), None)
    format_name = header.get("format") if header else None
    if format_name not in (None, SCHEDULER_FORMAT, *STATE_FORMATS):
        raise TraceError(f"{path}: unsupported trace format {format_name!r}")
    state_record = format_name in STATE_FORMATS or any(
        record.get("type") == "state" for record in records
    )
    result: dict[int, dict[str, Any]] = {}
    for record in records:
        parsed = _scheduler_record(record, path, state_record=state_record)
        if parsed is None:
            continue
        frame = parsed["frame"]
        if frame in result:
            raise TraceError(f"{path}: duplicate scheduler frame {frame}")
        result[frame] = parsed
    if not result:
        raise TraceError(f"{path}: no scheduler records with causal phase data")
    return result


def project_record(
    record: dict[str, Any],
    selected_phases: set[str],
) -> dict[str, Any]:
    names = [normalize_phase(name) for name in record["phase_names"]]
    pcs = list(record["phase_pcs"])
    if selected_phases:
        selected = {normalize_phase(name) for name in selected_phases}
        indexes = [index for index, name in enumerate(names) if name in selected]
        names = [names[index] for index in indexes]
        if pcs:
            pcs = [pcs[index] for index in indexes]
    return {
        "frame": record["frame"],
        "input": record["input"],
        "phase_names": names,
        "phase_pcs": pcs,
        "writer_pcs": record["writer_pcs"],
    }


def first_difference(left: Any, right: Any, path: str = "") -> tuple[str, Any, Any] | None:
    if type(left) is not type(right):
        return path or "$", left, right
    if isinstance(left, dict):
        for key in sorted(set(left) | set(right)):
            child = f"{path}.{key}" if path else key
            if key not in left or key not in right:
                return child, left.get(key), right.get(key)
            difference = first_difference(left[key], right[key], child)
            if difference:
                return difference
        return None
    if isinstance(left, list):
        for index in range(max(len(left), len(right))):
            child = f"{path}[{index}]"
            if index >= len(left) or index >= len(right):
                return child, left[index] if index < len(left) else None, right[index] if index < len(right) else None
            difference = first_difference(left[index], right[index], child)
            if difference:
                return difference
        return None
    return None if left == right else (path or "$", left, right)


def comparison_record(record: dict[str, Any], include_pcs: bool, include_writers: bool) -> dict[str, Any]:
    result: dict[str, Any] = {
        "input": record["input"],
        "phase_names": record["phase_names"],
    }
    if include_pcs:
        result["phase_pcs"] = record["phase_pcs"]
    if include_writers:
        result["writer_pcs"] = record["writer_pcs"]
    return result


def _display_phases(record: dict[str, Any] | None) -> str:
    if record is None:
        return "<missing>"
    return " -> ".join(record["phase_names"]) or "<empty>"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("genesis", type=Path, help="MAME state/scheduler trace")
    parser.add_argument("openaladdin", type=Path, help="native state/scheduler trace")
    parser.add_argument(
        "--phase",
        action="append",
        dest="phases",
        help="compare only this normalized phase family; repeat to form a projection",
    )
    parser.add_argument(
        "--include-pcs",
        action="store_true",
        help="also compare recovered phase entry PCs after phase normalization",
    )
    parser.add_argument(
        "--include-writers",
        action="store_true",
        help="also compare scheduler writer-PC provenance",
    )
    parser.add_argument(
        "--right-frame-offset",
        type=int,
        default=0,
        help="add this offset to native frame numbers before matching",
    )
    parser.add_argument(
        "--intersection",
        action="store_true",
        help="compare only frames carrying scheduler records on both sides",
    )
    parser.add_argument("--start-frame", type=int, default=None)
    parser.add_argument("--end-frame", type=int, default=None)
    args = parser.parse_args()

    genesis = {
        frame: project_record(record, set(args.phases or []))
        for frame, record in load_trace(args.genesis).items()
    }
    native = {}
    for frame, record in load_trace(args.openaladdin).items():
        projected = project_record(record, set(args.phases or []))
        projected["frame"] = frame + args.right_frame_offset
        native[frame + args.right_frame_offset] = projected

    frames = sorted(set(genesis) & set(native) if args.intersection else set(genesis) | set(native))
    if args.start_frame is not None:
        frames = [frame for frame in frames if frame >= args.start_frame]
    if args.end_frame is not None:
        frames = [frame for frame in frames if frame <= args.end_frame]
    if not frames:
        raise SystemExit("no scheduler frames selected")

    for frame in frames:
        left = genesis.get(frame)
        right = native.get(frame)
        if left is None or right is None:
            difference = (
                "scheduler_record",
                left is not None,
                right is not None,
            )
        else:
            difference = first_difference(
                comparison_record(left, args.include_pcs, args.include_writers),
                comparison_record(right, args.include_pcs, args.include_writers),
            )
        if difference:
            path, left_value, right_value = difference
            previous = [candidate for candidate in frames if candidate < frame and candidate in genesis and candidate in native]
            previous_frame = previous[-1] if previous else None
            print(f"First scheduler divergence: frame {frame}")
            print(path)
            print(f"  Genesis:      {json.dumps(left_value, sort_keys=True)}")
            print(f"  OpenAladdin:  {json.dumps(right_value, sort_keys=True)}")
            print(f"Last matching scheduler frame: {previous_frame if previous_frame is not None else 'none'}")
            print(
                f"Input: I[{previous_frame if previous_frame is not None else frame}] "
                f"Genesis={genesis.get(previous_frame if previous_frame is not None else frame, {}).get('input', 'none')} "
                f"OpenAladdin={native.get(previous_frame if previous_frame is not None else frame, {}).get('input', 'none')}"
            )
            print(f"Genesis phases:      {_display_phases(left)}")
            print(f"OpenAladdin phases:  {_display_phases(right)}")
            if left is not None and left.get("writer_pcs"):
                print(f"Genesis writer PCs observed: {json.dumps(left['writer_pcs'])}")
            if right is not None and right.get("writer_pcs"):
                print(f"OpenAladdin writer PCs observed: {json.dumps(right['writer_pcs'])}")
            return 1

    projection = "complete normalized sequence"
    if args.phases:
        projection = "phase projection: " + ", ".join(normalize_phase(name) for name in args.phases)
    print(f"Scheduler traces match for {len(frames)} frame(s) ({projection}).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
