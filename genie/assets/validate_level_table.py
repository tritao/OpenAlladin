#!/usr/bin/env python3
"""Validate tracked level-table metadata against generated ROM assets."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from genie.common import ROOT, load_yaml, parse_int


def _pair(value: Any, label: str) -> tuple[int, int]:
    if not isinstance(value, list) or len(value) != 2:
        raise ValueError(f"{label} must be a two-item list")
    return parse_int(value[0]), parse_int(value[1])


def validate(metadata_path: Path, generated_path: Path) -> list[str]:
    metadata = load_yaml(metadata_path) or {}
    generated = json.loads(generated_path.read_text(encoding="utf-8"))
    errors: list[str] = []
    table = metadata.get("rom_table", {})
    if parse_int(table.get("address", -1)) != parse_int(generated.get("table_offset", -2)):
        errors.append("ROM level-table address differs from generated levels.json")
    expected_count = parse_int(table.get("count", -1))
    if expected_count != int(generated.get("count", -2)):
        errors.append(f"level count differs: metadata={expected_count} generated={generated.get('count')}")

    generated_by_index = {int(row["index"]): row for row in generated.get("levels", [])}
    for spec in metadata.get("levels", []):
        index = int(spec["index"])
        row = generated_by_index.get(index)
        if row is None:
            errors.append(f"level {index}: missing generated record")
            continue
        entry = row.get("entry", {})
        camera_start = _pair(spec["camera_start"], f"level {index} camera_start")
        player_start = _pair(spec["player_start"], f"level {index} player_start")
        map_size = _pair(spec["map_size_blocks"], f"level {index} map_size_blocks")
        checks = {
            "start_x": camera_start[0],
            "start_y": camera_start[1],
            "offset_x": player_start[0],
            "offset_y": player_start[1],
            "block_width": map_size[0],
            "block_height": map_size[1],
            "music_id": parse_int(spec["music_id"]),
            "enter_function": parse_int(spec["enter_function"]),
            "exit_function": parse_int(spec["exit_function"]),
        }
        for field, expected in checks.items():
            actual = parse_int(entry.get(field, -1))
            if actual != expected:
                errors.append(f"level {index} {field}: metadata={expected} generated={actual}")
        for field in ("palette", "floor", "chars", "map", "block", "parallax", "animation"):
            if field not in spec:
                continue
            expected = parse_int(spec[field])
            actual = parse_int(entry.get(field, -1))
            if actual != expected:
                errors.append(f"level {index} {field}: metadata=0x{expected:06X} generated=0x{actual:06X}")
    if len(metadata.get("levels", [])) != expected_count:
        errors.append("tracked metadata does not contain the declared number of levels")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metadata", type=Path, default=ROOT / "re/assets/level_table.yml")
    parser.add_argument("--generated", type=Path, default=ROOT / "build/assets/levels.json")
    args = parser.parse_args()
    for path in (args.metadata, args.generated):
        if not path.is_file():
            raise SystemExit(f"required input not found: {path}")
    errors = validate(args.metadata.resolve(), args.generated.resolve())
    if errors:
        for error in errors:
            print(f"ERROR {error}")
        return 1
    print(f"validated level table: {len(load_yaml(args.metadata).get('levels', []))} entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
