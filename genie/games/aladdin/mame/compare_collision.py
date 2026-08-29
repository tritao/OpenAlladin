#!/usr/bin/env python3
"""Compare resolved animation collision records in two frame-state traces.

The full state comparator is useful once two implementations are close. This
focused comparator keeps collision work actionable by comparing only the
player rectangle and actor rectangles keyed by slot, then checking the first
requested actor transition frame.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

if __package__ in (None, ""):
    import sys

    sys.path.insert(0, str(Path(__file__).resolve().parents[4]))

from genie.core.mame.trace import first_difference, load_states


def actor_map(record: dict[str, Any]) -> dict[int, dict[str, Any]]:
    return {
        int(actor["slot"]): actor
        for actor in record.get("actors", [])
        if "slot" in actor
    }


def first_transition(
    states: dict[int, dict[str, Any]],
    slot: int,
    target_type: int | None,
) -> tuple[int, int, int | None] | None:
    previous: int | None = None
    for frame in sorted(states):
        actor = actor_map(states[frame]).get(slot)
        current = int(actor["type"]) if actor is not None and "type" in actor else None
        if target_type is not None:
            if current == target_type:
                return frame, current, previous
        elif current != previous and previous is not None:
            return frame, current if current is not None else -1, previous
        previous = current
    return None


def collision_value(
    record: dict[str, Any],
    slot: int,
) -> tuple[Any, Any]:
    actors = actor_map(record)
    actor = actors.get(slot)
    return record.get("player", {}).get("collision_box"), (
        actor.get("collision_box") if actor is not None else None
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("genesis", type=Path)
    parser.add_argument("openaladdin", type=Path)
    parser.add_argument(
        "--actor-slot",
        action="append",
        type=lambda value: int(value, 0),
        dest="actor_slots",
        help="compare this actor slot; repeat for multiple slots",
    )
    parser.add_argument(
        "--transition-type",
        type=lambda value: int(value, 0),
        help="report/check the first frame where each selected actor reaches this type",
    )
    args = parser.parse_args()

    _, genesis = load_states(args.genesis.resolve())
    _, native = load_states(args.openaladdin.resolve())
    actor_slots = set(args.actor_slots or [])
    if not actor_slots:
        for states in (genesis, native):
            for record in states.values():
                actor_slots.update(actor_map(record))

    frames = sorted(set(genesis) | set(native))
    for frame in frames:
        if frame not in genesis or frame not in native:
            print(f"First collision divergence: frame {frame}")
            print(f"  Genesis:      {'present' if frame in genesis else 'missing'}")
            print(f"  OpenAladdin:  {'present' if frame in native else 'missing'}")
            return 1

        genesis_player, native_player = (
            genesis[frame].get("player", {}).get("collision_box"),
            native[frame].get("player", {}).get("collision_box"),
        )
        difference = first_difference(genesis_player, native_player, "player.collision_box")
        if difference:
            path, left, right = difference
            print(f"First collision divergence: frame {frame}")
            print(path)
            print(f"  Genesis:      {json.dumps(left, sort_keys=True)}")
            print(f"  OpenAladdin:  {json.dumps(right, sort_keys=True)}")
            previous = [candidate for candidate in frames if candidate < frame]
            print(f"Previous matching frame: {previous[-1] if previous else 'none'}")
            return 1

        for slot in sorted(actor_slots):
            genesis_box = collision_value(genesis[frame], slot)[1]
            native_box = collision_value(native[frame], slot)[1]
            difference = first_difference(
                genesis_box,
                native_box,
                f"actors[slot={slot}].collision_box",
            )
            if difference:
                path, left, right = difference
                print(f"First collision divergence: frame {frame}")
                print(path)
                print(f"  Genesis:      {json.dumps(left, sort_keys=True)}")
                print(f"  OpenAladdin:  {json.dumps(right, sort_keys=True)}")
                previous = [candidate for candidate in frames if candidate < frame]
                print(f"Previous matching frame: {previous[-1] if previous else 'none'}")
                return 1

    for slot in sorted(actor_slots):
        genesis_transition = first_transition(genesis, slot, args.transition_type)
        native_transition = first_transition(native, slot, args.transition_type)
        print(
            f"actor slot {slot}: Genesis transition {genesis_transition[0] if genesis_transition else 'none'}, "
            f"OpenAladdin transition {native_transition[0] if native_transition else 'none'}"
        )
        if (genesis_transition is None) != (native_transition is None):
            return 1
        if genesis_transition and native_transition and genesis_transition[0] != native_transition[0]:
            return 1

    print(f"Collision traces match for {len(frames)} frame(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
