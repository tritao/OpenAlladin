#!/usr/bin/env python3
"""Rank changing 16-bit Genesis RAM words from a MAME trace.

This is deliberately a candidate finder, not a symbol generator.  A reported
address still needs confirmation with a second experiment and a write
watchpoint before it belongs in re/symbols/ram.yml.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


RAM_SIZE = 0x10000


def load_trace(trace_dir: Path) -> tuple[list[dict], bytes]:
    records = [
        json.loads(line)
        for line in (trace_dir / "trace_boot.jsonl").read_text().splitlines()
        if line.strip()
    ]
    ram = (trace_dir / "ram_frames.bin").read_bytes()
    frame_count = len(ram) // RAM_SIZE
    if len(ram) % RAM_SIZE:
        raise SystemExit("ram_frames.bin is not a whole number of RAM images")
    frame_records = [record for record in records if record.get("type") == "frame"]
    if len(frame_records) != frame_count:
        raise SystemExit(
            f"trace has {len(frame_records)} frame records but {frame_count} RAM images"
        )
    return frame_records, ram


def read_word(ram: bytes, frame: int, address: int) -> int:
    offset = frame * RAM_SIZE + address
    return struct.unpack_from(">H", ram, offset)[0]


def monotonic_score(values: list[int]) -> int:
    if len(values) < 2:
        return 0
    increasing = sum(left <= right for left, right in zip(values, values[1:]))
    decreasing = sum(left >= right for left, right in zip(values, values[1:]))
    return max(increasing, decreasing) - (len(values) - 1) // 2


def candidate_rows(records: list[dict], ram: bytes, token: str) -> list[dict]:
    active = [index for index, record in enumerate(records) if token in record["input"].split("+")]
    if len(active) < 2:
        raise SystemExit(f"need at least two frames containing input token {token!r}")

    active_start = min(active)
    active_end = max(active)
    before_start = max(0, active_start - len(active))
    after_end = min(len(records), active_end + len(active) + 1)
    rows = []

    for address in range(0, RAM_SIZE, 2):
        before = [read_word(ram, frame, address) for frame in range(before_start, active_start)]
        during = [read_word(ram, frame, address) for frame in active]
        after = [read_word(ram, frame, address) for frame in range(active_end + 1, after_end)]

        active_changes = sum(left != right for left, right in zip(during, during[1:]))
        total_changes = sum(
            read_word(ram, frame - 1, address) != read_word(ram, frame, address)
            for frame in range(1, len(records))
        )
        if active_changes == 0:
            continue

        edge_delta = abs(during[-1] - during[0])
        stable_context = max(0, len(before) - len(set(before))) + max(0, len(after) - len(set(after)))
        score = active_changes * 4 + min(edge_delta, 0x1000) // 16 + monotonic_score(during) * 2
        score += stable_context
        rows.append(
            {
                "address": address,
                "score": score,
                "active_changes": active_changes,
                "total_changes": total_changes,
                "unique_active": len(set(during)),
                "first": during[0],
                "last": during[-1],
                "edge_delta": edge_delta,
            }
        )

    return sorted(rows, key=lambda row: (row["score"], row["active_changes"]), reverse=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "trace_dir",
        nargs="?",
        type=Path,
        default=Path("build/re/traces"),
        help="directory containing trace_boot.jsonl and ram_frames.bin",
    )
    parser.add_argument(
        "--input",
        default="right",
        help="input token used for the active experiment interval",
    )
    parser.add_argument("--limit", type=int, default=40)
    args = parser.parse_args()

    records, ram = load_trace(args.trace_dir)
    rows = candidate_rows(records, ram, args.input.lower())

    active_frames = [
        index for index, record in enumerate(records) if args.input.lower() in record["input"].split("+")
    ]
    print(
        f"frames={len(records)} input={args.input!r} "
        f"active={min(active_frames)}..{max(active_frames)}"
    )
    print("address score active_changes total_changes unique first last edge_delta")
    for row in rows[: args.limit]:
        print(
            f"0x{row['address']:04X} {row['score']:5d} "
            f"{row['active_changes']:14d} {row['total_changes']:13d} "
            f"{row['unique_active']:6d} {row['first']:04X} {row['last']:04X} "
            f"{row['edge_delta']:9d}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
