#!/usr/bin/env python3
"""Compare versioned per-frame state JSONL files.

The comparator deliberately reports the first differing leaf rather than a
whole-record diff. This keeps a physics mismatch actionable when the state
record contains many unrelated actor fields.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


FORMAT = "openaladdin-frame-state-v1"


def load_states(path: Path) -> tuple[dict[str, Any] | None, dict[int, dict[str, Any]]]:
    header = None
    states: dict[int, dict[str, Any]] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise SystemExit(f"{path}:{line_number}: invalid JSON: {error}") from error
        if record.get("type") == "header":
            header = record
            continue
        if record.get("type") not in (None, "state", "frame_state"):
            continue
        if "frame" not in record:
            raise SystemExit(f"{path}:{line_number}: state record has no frame")
        states[int(record["frame"])] = record
    if header and header.get("format") not in (None, FORMAT):
        raise SystemExit(f"{path}: unsupported state format {header.get('format')!r}")
    if not states:
        raise SystemExit(f"{path}: no frame state records")
    return header, states


def first_difference(left: Any, right: Any, path: str = "") -> tuple[str, Any, Any] | None:
    if type(left) is not type(right):
        return path or "$", left, right
    if isinstance(left, dict):
        keys = sorted(set(left) | set(right))
        for key in keys:
            child = f"{path}.{key}" if path else str(key)
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


def field_value(record: dict[str, Any], field: str) -> Any:
    value: Any = record
    for component in field.split("."):
        match = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)(?:\[(\d+)\])?", component)
        if match is None:
            raise KeyError(field)
        name, index = match.groups()
        if not isinstance(value, dict) or name not in value:
            raise KeyError(field)
        value = value[name]
        if index is not None:
            if not isinstance(value, list):
                raise KeyError(field)
            slot = int(index)
            matches = [item for item in value if isinstance(item, dict) and item.get("slot") == slot]
            if not matches:
                raise KeyError(field)
            value = matches[0]
    return value


def selected_difference(left: dict[str, Any], right: dict[str, Any], fields: list[str]) -> tuple[str, Any, Any] | None:
    for field in fields:
        try:
            left_value = field_value(left, field)
        except KeyError:
            left_value = None
        try:
            right_value = field_value(right, field)
        except KeyError:
            right_value = None
        difference = first_difference(left_value, right_value, field)
        if difference:
            return difference
    return None


def print_divergence_context(
    frame: int,
    left: dict[int, dict[str, Any]],
    right: dict[int, dict[str, Any]],
    frames: list[int],
) -> None:
    left_record = left.get(frame, {})
    right_record = right.get(frame, {})
    print(f"Input: Genesis={left_record.get('input', 'none')} OpenAladdin={right_record.get('input', 'none')}")
    previous = [candidate for candidate in frames if candidate < frame and candidate in left and candidate in right]
    print(f"Previous matching frame: {previous[-1] if previous else 'none'}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("genesis", type=Path)
    parser.add_argument("openaladdin", type=Path)
    parser.add_argument(
        "--field",
        action="append",
        dest="fields",
        help="compare only this dotted state field; repeat for multiple fields",
    )
    args = parser.parse_args()
    left_header, left = load_states(args.genesis.resolve())
    right_header, right = load_states(args.openaladdin.resolve())
    if left_header and right_header:
        left_rom = left_header.get("rom_sha256")
        right_rom = right_header.get("rom_sha256")
        if left_rom and right_rom and left_rom != right_rom:
            raise SystemExit(f"ROM mismatch: Genesis={left_rom} OpenAladdin={right_rom}")

    frames = sorted(set(left) | set(right))
    for frame in frames:
        if frame not in left or frame not in right:
            print(f"First divergence: frame {frame}")
            print("state record")
            print(f"  Genesis:      {'present' if frame in left else 'missing'}")
            print(f"  OpenAladdin:  {'present' if frame in right else 'missing'}")
            print_divergence_context(frame, left, right, frames)
            return 1
        difference = (
            selected_difference(left[frame], right[frame], args.fields)
            if args.fields
            else first_difference(left[frame], right[frame])
        )
        if difference:
            path, genesis_value, openaladdin_value = difference
            print(f"First divergence: frame {frame}")
            print(path)
            print(f"  Genesis:      {json.dumps(genesis_value, sort_keys=True)}")
            print(f"  OpenAladdin:  {json.dumps(openaladdin_value, sort_keys=True)}")
            print_divergence_context(frame, left, right, frames)
            return 1

    print(f"Traces match for {len(frames)} frame(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
