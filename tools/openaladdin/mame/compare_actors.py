#!/usr/bin/env python3
"""Compare active actor slots in two OpenAladdin frame-state traces.

Actor records have a few client-local fields (for example native lifetime
bookkeeping), so this comparator intentionally checks the shared Genesis
actor-table fields rather than requiring byte-for-byte JSON equality.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

from compare_state import load_states


DEFAULT_FIELDS = [
    "type",
    "x",
    "y",
    "movement_flags",
    "facing_x_flip",
    "facing_y_flip",
    "frame_ptr",
    "animation_pc",
    "movement_pc",
    "movement_loop_pc",
    "movement_loop_timer",
    "movement_word_18",
    "movement_word_1a",
    "animation_timer",
    "movement_return_pc",
    "flags",
    "movement_command_timer",
]


def actor_table(record: dict[str, Any]) -> dict[int, dict[str, Any]]:
    result: dict[int, dict[str, Any]] = {}
    for actor in record.get("actors", []):
        if not isinstance(actor, dict) or "slot" not in actor:
            continue
        result[int(actor["slot"])] = actor
    return result


def first_difference(left: Any, right: Any, path: str) -> tuple[str, Any, Any] | None:
    if type(left) is not type(right):
        return path, left, right
    if isinstance(left, dict):
        for key in sorted(set(left) | set(right)):
            child = f"{path}.{key}"
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
    return None if left == right else (path, left, right)


def actor_difference(
    left: dict[str, Any],
    right: dict[str, Any],
    fields: list[str],
    include_player: bool,
) -> tuple[str, Any, Any] | None:
    left_actors = actor_table(left)
    right_actors = actor_table(right)
    for slot in sorted(set(left_actors) | set(right_actors)):
        if slot == 0 and not include_player:
            continue
        left_actor = left_actors.get(slot)
        right_actor = right_actors.get(slot)
        if left_actor is None or right_actor is None:
            return f"actors[{slot}]", left_actor, right_actor
        for field in fields:
            difference = first_difference(
                left_actor.get(field),
                right_actor.get(field),
                f"actors[{slot}].{field}",
            )
            if difference:
                return difference
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("genesis", type=Path)
    parser.add_argument("openaladdin", type=Path)
    parser.add_argument(
        "--field",
        action="append",
        dest="fields",
        help="shared actor field to compare; repeat for multiple fields",
    )
    parser.add_argument(
        "--include-player",
        action="store_true",
        help="also compare actor-table slot 0 (the player)",
    )
    args = parser.parse_args()

    left_header, left = load_states(args.genesis.resolve())
    right_header, right = load_states(args.openaladdin.resolve())
    if left_header and right_header:
        left_rom = left_header.get("rom_sha256")
        right_rom = right_header.get("rom_sha256")
        if left_rom and right_rom and left_rom != right_rom:
            raise SystemExit(f"ROM mismatch: Genesis={left_rom} OpenAladdin={right_rom}")

    fields = args.fields or DEFAULT_FIELDS
    frames = sorted(set(left) | set(right))
    for frame in frames:
        if frame not in left or frame not in right:
            difference = ("state", left.get(frame), right.get(frame))
        else:
            difference = actor_difference(
                left[frame], right[frame], fields, args.include_player
            )
        if difference:
            path, genesis_value, openaladdin_value = difference
            print(f"First actor divergence: frame {frame}")
            print(path)
            print(f"  Genesis:      {genesis_value!r}")
            print(f"  OpenAladdin:  {openaladdin_value!r}")
            previous = [candidate for candidate in frames if candidate < frame and candidate in left and candidate in right]
            print(f"Previous matching frame: {previous[-1] if previous else 'none'}")
            print(
                "Input: Genesis=%s OpenAladdin=%s"
                % (left.get(frame, {}).get("input", "none"), right.get(frame, {}).get("input", "none"))
            )
            return 1

    print(f"Actor tables match for {len(frames)} frame(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
