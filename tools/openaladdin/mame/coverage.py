#!/usr/bin/env python3
"""Merge sampled MAME PCs into a versioned runtime-coverage report.

The current Lua probe records the CPU PC at the frame boundary and, when
requested, indirect handler edges from MAME's debugger.  The PC stream is
sampled execution coverage, not a basic-block trace; keeping that distinction
in the report makes it safe to grow the format later when a MAME execution tap
is available.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
from typing import Any, Iterable

from openaladdin.common import ROOT, parse_int, write_json


FORMAT = "openaladdin-runtime-coverage-v2"

EDGE_PATTERN = re.compile(
    r"OPENALADDIN_EDGE TABLE=(?P<table>[A-Za-z0-9_+]+) "
    r"TARGET=(?P<target>[0-9A-Fa-f]+) "
    r"RETURN=(?P<return_address>[0-9A-Fa-f]+) "
    r"FRAME=(?P<frame>[0-9A-Fa-f]+)"
)


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"{path}:{line_number}: invalid JSON: {error}") from error
        if not isinstance(value, dict):
            raise ValueError(f"{path}:{line_number}: trace record is not an object")
        records.append(value)
    return records


def _trace_path(path: Path) -> Path:
    return path / "trace_boot.jsonl" if path.is_dir() else path


def _scenario_name(trace_path: Path, trace_root: Path | None) -> str:
    directory = trace_path.parent.resolve()
    if trace_root is not None:
        try:
            relative = directory.relative_to(trace_root.resolve())
            if str(relative) != ".":
                return relative.as_posix()
        except ValueError:
            pass
    return directory.name


def _pc(value: Any) -> int:
    return parse_int(value) & 0xFFFFFF


def _read_edges(path: Path, scenario: str) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    edges = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = EDGE_PATTERN.search(line)
        if not match:
            continue
        target = int(match.group("target"), 16) & 0xFFFFFF
        return_address = int(match.group("return_address"), 16) & 0xFFFFFF
        # The indirect handler calls in this ROM are two-byte JSR forms;
        # d@sp is therefore the address immediately after the dispatch.
        edges.append({
            "tables": match.group("table").split("+"),
            "source": max(0, return_address - 2),
            "target": target,
            "return_address": return_address,
            "frame": int(match.group("frame"), 16),
            "scenario": scenario,
        })
    return edges


def discover_trace_dirs(trace_root: Path) -> list[Path]:
    """Find trace directories without treating generated files as inputs."""

    root = trace_root.resolve()
    if not root.is_dir():
        raise ValueError(f"trace root does not exist: {root}")
    return sorted({path.parent.resolve() for path in root.rglob("trace_boot.jsonl")})


def merge_coverage(
    trace_dirs: Iterable[Path],
    output: Path,
    *,
    trace_root: Path | None = None,
) -> dict[str, Any]:
    """Merge one or more frame-PC traces into ``output``."""

    scenarios: list[dict[str, Any]] = []
    pcs: dict[int, dict[str, Any]] = {}
    names: set[str] = set()
    rom_hashes: set[str] = set()
    total_frames = 0
    total_samples = 0
    missing_pc_frames = 0
    zero_pc_frames = 0
    edge_samples: list[dict[str, Any]] = []

    paths = [_trace_path(Path(path).resolve()) for path in trace_dirs]
    if not paths:
        raise ValueError("no MAME traces found")
    for trace_path in paths:
        if not trace_path.is_file():
            raise ValueError(f"trace not found: {trace_path}")
        records = _read_jsonl(trace_path)
        header = next((record for record in records if record.get("type") == "header"), None)
        frames = [record for record in records if record.get("type") == "frame"]
        if header is None or not frames:
            raise ValueError(f"trace has no header/frames: {trace_path.parent}")

        name = _scenario_name(trace_path, trace_root)
        if name in names:
            raise ValueError(f"duplicate coverage scenario name: {name}")
        names.add(name)
        rom_sha256 = str(header.get("rom_sha256") or "")
        if rom_sha256:
            rom_hashes.add(rom_sha256)

        scenario_pcs: set[int] = set()
        scenario_samples = 0
        for fallback_frame, frame in enumerate(frames):
            frame_number = int(frame.get("frame", fallback_frame))
            if "pc" not in frame:
                missing_pc_frames += 1
                continue
            address = _pc(frame["pc"])
            if address == 0:
                zero_pc_frames += 1
                continue
            scenario_samples += 1
            total_samples += 1
            scenario_pcs.add(address)
            entry = pcs.setdefault(address, {
                "scenarios": set(),
                "sample_count": 0,
                "first_frame": frame_number,
                "last_frame": frame_number,
            })
            entry["scenarios"].add(name)
            entry["sample_count"] += 1
            entry["first_frame"] = min(entry["first_frame"], frame_number)
            entry["last_frame"] = max(entry["last_frame"], frame_number)

        scenario_edges = _read_edges(trace_path.parent / "debug.log", name)
        scenarios.append({
            "name": name,
            "trace_dir": str(trace_path.parent),
            "frame_count": len(frames),
            "pc_sample_count": scenario_samples,
            "unique_pc_count": len(scenario_pcs),
            "edge_sample_count": len(scenario_edges),
            "rom_sha256": rom_sha256,
        })
        edge_samples.extend(scenario_edges)
        total_frames += len(frames)

    if len(rom_hashes) > 1:
        raise ValueError(f"coverage traces use different ROMs: {sorted(rom_hashes)}")

    normalized_pcs = {
        f"0x{address:06X}": {
            "scenarios": sorted(value["scenarios"]),
            "sample_count": value["sample_count"],
            "first_frame": value["first_frame"],
            "last_frame": value["last_frame"],
        }
        for address, value in sorted(pcs.items())
    }
    edges: dict[tuple[int, int], dict[str, Any]] = {}
    for sample in edge_samples:
        key = (sample["source"], sample["target"])
        entry = edges.setdefault(key, {
            "source": sample["source"],
            "target": sample["target"],
            "tables": set(),
            "scenarios": set(),
            "sample_count": 0,
            "first_frame": sample["frame"],
            "last_frame": sample["frame"],
        })
        entry["tables"].update(sample["tables"])
        entry["scenarios"].add(sample["scenario"])
        entry["sample_count"] += 1
        entry["first_frame"] = min(entry["first_frame"], sample["frame"])
        entry["last_frame"] = max(entry["last_frame"], sample["frame"])
    normalized_edges = [
        {
            "source": f"0x{value['source']:06X}",
            "target": f"0x{value['target']:06X}",
            "tables": sorted(value["tables"]),
            "scenarios": sorted(value["scenarios"]),
            "sample_count": value["sample_count"],
            "first_frame": value["first_frame"],
            "last_frame": value["last_frame"],
        }
        for _, value in sorted(edges.items())
    ]
    report = {
        "format": FORMAT,
        "capture": "frame-pc-sample+indirect-edge",
        "rom_sha256": next(iter(rom_hashes), ""),
        "scenarios": sorted(scenarios, key=lambda item: item["name"]),
        "pcs": normalized_pcs,
        "edges": normalized_edges,
        "summary": {
            "scenario_count": len(scenarios),
            "frame_count": total_frames,
            "pc_sample_count": total_samples,
            "unique_pc_count": len(normalized_pcs),
            "missing_pc_frames": missing_pc_frames,
            "zero_pc_frames": zero_pc_frames,
            "edge_sample_count": len(edge_samples),
            "unique_edge_count": len(normalized_edges),
        },
    }
    write_json(output, report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "trace_dirs",
        nargs="*",
        type=Path,
        help="trace directories or trace_boot.jsonl files; defaults to --trace-root",
    )
    parser.add_argument(
        "--trace-root",
        type=Path,
        default=ROOT / "build/re/traces",
        help="root recursively containing scenario traces",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "build/re/coverage.json",
    )
    args = parser.parse_args()
    root = args.trace_root.resolve()
    paths = [path.resolve() for path in args.trace_dirs] if args.trace_dirs else discover_trace_dirs(root)
    try:
        report = merge_coverage(paths, args.output.resolve(), trace_root=root)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(str(error)) from error
    summary = report["summary"]
    print(
        f"coverage: {summary['scenario_count']} scenario(s), "
        f"{summary['pc_sample_count']} PC sample(s), "
        f"{summary['unique_pc_count']} unique PC(s), "
        f"{summary.get('unique_edge_count', 0)} indirect edge(s)"
    )
    print(f"report: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
