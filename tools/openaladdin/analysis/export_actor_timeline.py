#!/usr/bin/env python3
"""Export a frame range of MAME actor snapshots for native replay tests."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--trace",
        type=Path,
        default=Path("build/re/actor-flags-final/state.jsonl"),
    )
    parser.add_argument("--start-frame", type=int, required=True)
    parser.add_argument("--end-frame", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.end_frame < args.start_frame:
        raise SystemExit("--end-frame must be at least --start-frame")

    states = {}
    with args.trace.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state":
                frame = int(record["frame"])
                if args.start_frame <= frame <= args.end_frame:
                    states[frame] = record

    expected = set(range(args.start_frame, args.end_frame + 1))
    missing = sorted(expected - states.keys())
    if missing:
        raise SystemExit(f"missing state frames: {missing[:8]}")

    lines = [
        "# openaladdin-actor-timeline-v1",
        f"# Source state frames {args.start_frame}..{args.end_frame} (inclusive).",
        "# Frame numbers are rebased to zero for native replay.",
        "# @frame N followed by: slot type x y movement_pc frame_ptr animation_pc flags",
    ]
    for relative, source_frame in enumerate(range(args.start_frame, args.end_frame + 1)):
        lines.append(f"@frame {relative}")
        actors = sorted(states[source_frame].get("actors", []), key=lambda actor: int(actor["slot"]))
        for actor in actors:
            lines.append(
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

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(
        f"exported actor timeline frames {args.start_frame}..{args.end_frame} "
        f"to {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
