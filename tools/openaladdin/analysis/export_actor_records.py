#!/usr/bin/env python3
"""Export one captured MAME actor snapshot as a native actor seed table."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--trace",
        type=Path,
        default=Path("build/re/actor-flags-final/state.jsonl"),
        help="MAME state.jsonl containing actor snapshots",
    )
    parser.add_argument("--frame", type=int, required=True, help="exact state frame to export")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("re/actors/level01.tsv"),
        help="native actor seed table to write",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    selected = None
    with args.trace.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state" and record.get("frame") == args.frame:
                selected = record
                break

    if selected is None:
        raise SystemExit(f"state frame {args.frame} not found in {args.trace}")

    actors = sorted(selected.get("actors", []), key=lambda actor: int(actor["slot"]))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# openaladdin-actor-table-v1",
        f"# Captured level-01 actor snapshot, MAME state frame {args.frame}.",
        "# This is a data-driven interaction seed, not a complete actor spawner.",
        "# slot type x y movement_pc frame_ptr animation_pc flags [facing_x_flip [facing_y_flip [movement_command_timer]]]",
    ]
    for actor in actors:
        line = (
            "{slot} {type} {x} {y} {movement_pc:#x} {frame_ptr:#x} {animation_pc:#x} {flags:#x}".format(
                slot=int(actor["slot"]),
                type=int(actor.get("type", 0)),
                x=int(actor.get("x", 0)),
                y=int(actor.get("y", 0)),
                movement_pc=int(actor.get("movement_pc", 0)),
                frame_ptr=int(actor.get("frame_ptr", 0)),
                animation_pc=int(actor.get("animation_pc", 0)),
                flags=int(actor.get("flags", 0)),
            )
        )
        if "facing_x_flip" in actor:
            line += f" {int(actor.get('facing_x_flip', 0)):#x}"
        if "facing_y_flip" in actor:
            line += f" {int(actor.get('facing_y_flip', 0)):#x}"
        if "movement_command_timer" in actor:
            line += f" {int(actor.get('movement_command_timer', 0)):#x}"
        lines.append(line)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"exported {len(actors)} actors from frame {args.frame} to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
