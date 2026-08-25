#!/usr/bin/env python3
"""Merge scenario MAME traces and dynamic RNC-loader reports."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
from typing import Any, Iterable

from openaladdin.common import ROOT, write_json


TRACE_FILES = (
    ("ram_frames.bin", "ram_size"),
    ("vdp_vram_frames.bin", "vdp_vram_bytes"),
    ("vdp_cram_frames.bin", "vdp_cram_bytes"),
    ("vdp_vsram_frames.bin", "vdp_vsram_bytes"),
    ("vdp_regs_frames.bin", "vdp_regs_bytes"),
)


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    return [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]


def _resolve(path: str | Path) -> Path:
    value = Path(path)
    return value.resolve() if value.is_absolute() else (ROOT / value).resolve()


def _relative(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(ROOT))
    except ValueError:
        return str(path.resolve())


def _scenario_path(scenario: dict[str, Any], key: str) -> Path:
    value = scenario.get(key)
    if not value:
        raise ValueError(f"scenario {scenario.get('name', '<unnamed>')} has no {key}")
    return _resolve(value)


def _merge_load_reports(
    scenarios: list[dict[str, Any]],
    output: Path,
    frame_offsets: dict[str, int],
) -> dict[str, Any]:
    reports = []
    for scenario in scenarios:
        path = _scenario_path(scenario, "load_trace")
        if not path.is_file():
            raise FileNotFoundError(f"RNC load report not found: {path}")
        reports.append((scenario, json.loads(path.read_text(encoding="utf-8"))))

    events: list[dict[str, Any]] = []
    rom: dict[str, Any] = {}
    loader_analysis = None
    for scenario, report in reports:
        if not rom:
            rom = report.get("rom", {})
        elif report.get("rom", {}).get("sha1") != rom.get("sha1"):
            raise ValueError(f"scenario {scenario['name']} uses a different ROM")
        loader_analysis = loader_analysis or report.get("loader_analysis")
        for event in report.get("events", []):
            merged = dict(event)
            local_frame = int(event.get("frame", 0))
            merged["scenario"] = scenario["name"]
            merged["scenario_frame"] = local_frame
            merged["global_frame"] = frame_offsets[scenario["name"]] + local_frame
            events.append(merged)

    destinations: dict[str, int] = {}
    for event in events:
        destination = event["destination"]
        destinations[destination] = destinations.get(destination, 0) + 1
    blocks = sorted(
        {event["source"] for event in events if event.get("block")},
        key=lambda value: int(value, 16),
    )
    report = {
        "format": "openaladdin-rnc-load-trace-v1",
        "rom": rom,
        "matrix": True,
        "scenarios": [scenario["name"] for scenario, _ in reports],
        "loader_analysis": loader_analysis,
        "summary": {
            "event_count": len(events),
            "unique_call_count": len({event["call_address"] for event in events}),
            "unique_source_count": len({event["source"] for event in events}),
            "known_block_count": len(blocks),
            "unknown_source_count": sum(event.get("block") is None for event in events),
            "static_call_match_count": sum(event["static_call_match"] for event in events),
            "static_source_match_count": sum(event["source_matches_static"] for event in events),
            "static_destination_match_count": sum(event["destination_matches_static"] for event in events),
            "by_destination": dict(sorted(destinations.items())),
        },
        "events": events,
        "observed_blocks": blocks,
    }
    write_json(output / "rnc_loads.json", report)
    return report


def _append_binary(source: Path, target) -> None:
    with source.open("rb") as stream:
        shutil.copyfileobj(stream, target, length=1024 * 1024)


def _merge_trace_data(
    scenarios: list[dict[str, Any]],
    output: Path,
) -> tuple[list[dict[str, Any]], dict[str, int], list[dict[str, Any]]]:
    output.mkdir(parents=True, exist_ok=True)
    first_header: dict[str, Any] | None = None
    merged_frames: list[dict[str, Any]] = []
    merged_events: list[dict[str, Any]] = []
    frame_offsets: dict[str, int] = {}
    total_frames = 0

    targets = {
        name: (output / name).open("wb")
        for name, _ in TRACE_FILES
    }
    try:
        for scenario in scenarios:
            trace_dir = _scenario_path(scenario, "trace_dir")
            records = _read_jsonl(trace_dir / "trace_boot.jsonl")
            header = next((record for record in records if record.get("type") == "header"), None)
            frames = [record for record in records if record.get("type") == "frame"]
            if header is None or not frames:
                raise ValueError(f"trace has no header/frames: {trace_dir}")
            if first_header is None:
                first_header = dict(header)
            for key in ("ram_size", "vdp_vram_bytes", "vdp_cram_bytes", "vdp_vsram_bytes", "vdp_regs_bytes"):
                if header.get(key) != first_header.get(key):
                    raise ValueError(f"trace header mismatch for {scenario['name']}: {key}")

            frame_offsets[scenario["name"]] = total_frames
            for local_index, frame in enumerate(frames):
                merged = dict(frame)
                merged["frame"] = total_frames + local_index
                merged["scenario"] = scenario["name"]
                merged["scenario_frame"] = int(frame.get("frame", local_index))
                merged_frames.append(merged)
            for record in records:
                if record.get("type") != "scene_state":
                    continue
                merged = dict(record)
                local_frame = int(record.get("frame", 0))
                merged["frame"] = total_frames + local_frame
                merged["scenario"] = scenario["name"]
                merged["scenario_frame"] = local_frame
                merged_events.append(merged)

            for filename, size_key in TRACE_FILES:
                source = trace_dir / filename
                if not source.is_file():
                    raise FileNotFoundError(f"trace file not found: {source}")
                expected = len(frames) * int(header[size_key])
                actual = source.stat().st_size
                if actual != expected:
                    raise ValueError(
                        f"{source} has {actual} bytes; expected {expected} for {len(frames)} frames"
                    )
                _append_binary(source, targets[filename])

            writes = _read_jsonl(trace_dir / "vdp_writes.jsonl")
            for write in writes:
                merged_write = dict(write)
                if "frame" in merged_write:
                    merged_write["scenario"] = scenario["name"]
                    merged_write["scenario_frame"] = merged_write["frame"]
                    merged_write["frame"] = frame_offsets[scenario["name"]] + int(merged_write["frame"])
                with (output / "vdp_writes.jsonl").open("a", encoding="utf-8") as stream:
                    stream.write(json.dumps(merged_write, separators=(",", ":")) + "\n")
            total_frames += len(frames)
    finally:
        for stream in targets.values():
            stream.close()

    if first_header is None:
        raise ValueError("matrix contains no scenarios")
    first_header["frame_limit"] = total_frames
    first_header["capture_matrix"] = True
    first_header["capture_scenario_count"] = len(scenarios)
    with (output / "trace_boot.jsonl").open("w", encoding="utf-8") as stream:
        stream.write(json.dumps(first_header, separators=(",", ":")) + "\n")
        records = merged_frames + merged_events
        records.sort(key=lambda record: (
            int(record.get("frame", 0)),
            0 if record.get("type") == "scene_state" else 1,
        ))
        for record in records:
            stream.write(json.dumps(record, separators=(",", ":")) + "\n")
    return merged_frames, frame_offsets, merged_events


def merge_matrix(matrix_path: Path, output: Path) -> dict[str, Any]:
    matrix = json.loads(matrix_path.read_text(encoding="utf-8"))
    scenarios = [scenario for scenario in matrix.get("scenarios", []) if scenario.get("status") == "completed"]
    if not scenarios:
        raise ValueError("matrix has no completed scenarios")
    output = output.resolve()
    if output.exists():
        for path in output.iterdir():
            if path.is_file():
                path.unlink()
    frames, frame_offsets, scene_events = _merge_trace_data(scenarios, output)
    load_report = _merge_load_reports(scenarios, output, frame_offsets)
    result = {
        "format": "openaladdin-mame-capture-matrix-v1",
        "matrix": str(matrix_path.resolve()),
        "output": str(output),
        "scenarios": [scenario["name"] for scenario in scenarios],
        "frame_count": len(frames),
        "scene_state_events": len(scene_events),
        "loader_events": load_report["summary"]["event_count"],
        "observed_blocks": load_report["summary"]["known_block_count"],
    }
    write_json(output / "matrix_trace.json", result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("matrix", type=Path, help="capture-matrix manifest JSON")
    parser.add_argument("--output", type=Path, required=True, help="combined trace output directory")
    args = parser.parse_args()
    result = merge_matrix(args.matrix.resolve(), args.output)
    print(f"merged scenarios: {len(result['scenarios'])}")
    print(f"merged frames: {result['frame_count']}")
    print(f"dynamic loader events: {result['loader_events']}")
    print(f"observed RNC blocks: {result['observed_blocks']}")
    print(f"merged trace: {Path(result['output'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
