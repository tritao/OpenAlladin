#!/usr/bin/env python3
"""Validate controlled scene-state level-loader traces against the ROM table."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from genie.common import ROOT, load_yaml, parse_int


def _states(path: Path) -> dict[int, dict[str, Any]]:
    result: dict[int, dict[str, Any]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        record = json.loads(line)
        if record.get("type") == "state":
            result[int(record["frame"])] = record
    return result


def validate(campaign_path: Path) -> list[str]:
    campaign = json.loads(campaign_path.read_text(encoding="utf-8"))
    metadata = load_yaml(ROOT / "re/assets/level_table.yml") or {}
    specs = {int(row["index"]): row for row in metadata.get("levels", [])}
    errors: list[str] = []
    segments = campaign.get("segments", [])
    if len(segments) != parse_int(metadata.get("rom_table", {}).get("count", -1)):
        errors.append(f"campaign has {len(segments)} segments; expected {len(specs)}")

    for segment in segments:
        index = parse_int(segment["selected_scene_state"])
        spec = specs.get(index)
        if spec is None:
            errors.append(f"scene state {index}: missing level-table metadata")
            continue
        trace = ROOT / segment["trace_dir"] / "state.jsonl"
        if not trace.is_file():
            errors.append(f"scene state {index}: missing trace {trace}")
            continue
        states = _states(trace)
        loader_frame = parse_int(segment["loader_frame"])
        settled_frame = parse_int(segment["settled_frame"])
        loader = states.get(loader_frame)
        settled = states.get(settled_frame)
        if loader is None:
            errors.append(f"scene state {index}: missing loader frame {loader_frame}")
            continue
        if settled is None:
            errors.append(f"scene state {index}: missing settled frame {settled_frame}")
            continue

        expected_camera = tuple(parse_int(value) for value in spec["camera_start"])
        actual_camera = (
            parse_int(loader["camera"]["x"]),
            parse_int(loader["camera"]["y"]),
        )
        if actual_camera != expected_camera:
            errors.append(
                f"scene state {index}: camera at frame {loader_frame} "
                f"{actual_camera} != {expected_camera}"
            )
        if parse_int(loader["scene"]["state"]) != index:
            errors.append(f"scene state {index}: loader frame selected a different scene")
        if parse_int(settled["scene"]["state"]) != index:
            errors.append(f"scene state {index}: settled frame selected a different scene")

        expected_dimensions = (
            (parse_int(spec["map_size_blocks"][0]) * 16) & 0xFFFF,
            (parse_int(spec["map_size_blocks"][1]) * 16) & 0xFFFF,
        )
        actual_dimensions = (
            parse_int(settled["camera"]["level_width"]),
            parse_int(settled["camera"]["level_height"]),
        )
        if actual_dimensions != expected_dimensions:
            errors.append(
                f"scene state {index}: settled dimensions {actual_dimensions} "
                f"!= 16-bit table projection {expected_dimensions}"
            )
        print(
            f"scene {index:02d}: camera={actual_camera} "
            f"dimensions={actual_dimensions} OK"
        )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--campaign",
        type=Path,
        default=ROOT / "re/mame/campaigns/20260827-level-loader-matrix-v1.json",
    )
    args = parser.parse_args()
    campaign_path = args.campaign if args.campaign.is_absolute() else ROOT / args.campaign
    if not campaign_path.is_file():
        raise SystemExit(f"campaign not found: {campaign_path}")
    errors = validate(campaign_path.resolve())
    if errors:
        for error in errors:
            print(f"ERROR {error}")
        return 1
    print("validated controlled level-loader matrix")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
