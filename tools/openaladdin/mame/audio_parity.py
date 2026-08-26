#!/usr/bin/env python3
"""Compare normalized MAME and native audio traces.

The comparison is deliberately made at the deterministic command and chip-bus
levels. Host audio samples are not stable across SDL backends and are not
useful for locating a driver mismatch.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
from typing import Any, Iterable

try:
    from .audio_trace import read_commands, read_jsonl
except ImportError:  # pragma: no cover - supports direct script execution
    from audio_trace import read_commands, read_jsonl


def as_int(value: Any, default: int = 0) -> int:
    if value is None:
        return default
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    text = str(value).strip()
    return int(text, 0) if text.lower().startswith("0x") else int(text)


def native_records(path: Path) -> list[dict[str, Any]]:
    records = read_jsonl(path)
    if not records:
        raise SystemExit(f"{path}: no native audio records")
    return records


def mame_write_records(trace_dir: Path, source: str) -> list[dict[str, Any]]:
    writes = read_jsonl(trace_dir / "sound_writes.jsonl")
    if not writes:
        summary_path = trace_dir / "audio_summary.json"
        if summary_path.is_file():
            summary = json.loads(summary_path.read_text(encoding="utf-8"))
            writes = summary.get("sound_writes", [])
    return [record for record in writes if record.get("source") == source]


def normalize_write(record: dict[str, Any]) -> tuple[int, str, int | None, int]:
    kind = str(record.get("kind", "unknown")).lower()
    port = as_int(record.get("port")) if kind == "ym2612" else None
    byte = as_int(record.get("byte", record.get("data"))) & 0xFF
    return as_int(record.get("frame")), kind, port, byte


def normalized_native_writes(records: Iterable[dict[str, Any]]) -> list[tuple[int, str, int | None, int]]:
    return [
        normalize_write(record)
        for record in records
        if record.get("type") == "audio_write"
    ]


def normalized_mame_writes(
    records: Iterable[dict[str, Any]],
) -> list[tuple[int, str, int | None, int]]:
    return [normalize_write(record) for record in records]


def normalized_native_commands(
    records: Iterable[dict[str, Any]],
    frame_offset: int = 0,
) -> list[tuple[int, str, int]]:
    result = []
    for record in records:
        if record.get("type") != "audio_command" or record.get("sound_id") is None:
            continue
        result.append((
            as_int(record.get("frame")) + frame_offset,
            str(record.get("kind", "SOUND")).upper(),
            as_int(record.get("sound_id")) & 0xFF,
        ))
    return result


def normalized_mame_commands(
    trace_dir: Path,
    frame_offset: int = 0,
) -> list[tuple[int, str, int]]:
    return [
        (
            record["frame"] + frame_offset,
            str(record["kind"]).upper(),
            record["id"] & 0xFF,
        )
        for record in read_commands(trace_dir / "debug.log")
    ]


def first_difference(
    expected: list[tuple[Any, ...]],
    actual: list[tuple[Any, ...]],
) -> tuple[int, tuple[Any, ...] | None, tuple[Any, ...] | None] | None:
    for index in range(max(len(expected), len(actual))):
        left = expected[index] if index < len(expected) else None
        right = actual[index] if index < len(actual) else None
        if left != right:
            return index, left, right
    return None


def format_record(record: tuple[Any, ...] | None) -> str:
    if record is None:
        return "<missing>"
    if len(record) == 4:
        frame, kind, port, byte = record
        port_text = "" if port is None else f" port={port:02X}"
        return f"frame={frame}{port_text} {kind} byte={byte:02X}"
    frame, kind, sound_id = record
    return f"frame={frame} {kind} id={sound_id:02X}"


def compare_records(
    label: str,
    expected: list[tuple[Any, ...]],
    actual: list[tuple[Any, ...]],
) -> bool:
    difference = first_difference(expected, actual)
    if difference is None:
        print(f"{label}: match ({len(expected)} records)")
        return True
    index, left, right = difference
    print(f"{label}: mismatch at record {index}")
    print(f"  MAME:   {format_record(left)}")
    print(f"  native: {format_record(right)}")
    print(f"  counts: MAME={len(expected)} native={len(actual)}")
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mame_trace", type=Path, help="MAME trace directory")
    parser.add_argument("native_trace", type=Path, help="native JSONL audio trace")
    parser.add_argument(
        "--section",
        choices=("writes", "commands", "all"),
        default="all",
        help="compare chip writes, sound commands, or both (default: all)",
    )
    parser.add_argument(
        "--mame-source",
        default="z80",
        help="MAME sound-write source to compare (default: z80)",
    )
    parser.add_argument(
        "--native-frame-offset",
        type=int,
        default=0,
        help="add this offset to native command frames before comparison",
    )
    args = parser.parse_args()

    mame_trace = args.mame_trace.resolve()
    native_trace = native_records(args.native_trace.resolve())
    mame_writes = mame_write_records(mame_trace, args.mame_source)
    native_writes = normalized_native_writes(native_trace)
    native_events = [record for record in native_trace if record.get("type") == "driver_event"]
    event_kinds = Counter(str(record.get("kind", "unknown")) for record in native_events)
    print(f"native driver events: {len(native_events)} {dict(sorted(event_kinds.items()))}")

    passed = True
    compared = 0
    if args.section in ("writes", "all"):
        passed &= compare_records(
            "writes",
            normalized_mame_writes(mame_writes),
            native_writes,
        )
        compared += 1

    if args.section in ("commands", "all"):
        mame_commands = normalized_mame_commands(mame_trace)
        native_commands = normalized_native_commands(
            native_trace, args.native_frame_offset
        )
        if not mame_commands and args.section == "all":
            print("commands: skipped (MAME debug.log has no decoded command records)")
        else:
            passed &= compare_records("commands", mame_commands, native_commands)
            compared += 1

    if compared == 0:
        print("no comparable audio sections")
        return 2
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
