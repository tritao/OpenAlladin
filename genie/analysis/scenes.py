#!/usr/bin/env python3
"""Correlate runtime scene-state transitions with dynamic RNC uploads."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from genie.common import ROOT, load_yaml, parse_int, write_json


def _value(value: Any) -> int:
    return parse_int(value)


def _hex(value: int) -> str:
    return f"0x{value:06X}"


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        raise SystemExit(f"trace file not found: {path}")
    return [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]


def _resource_map(metadata: dict[str, Any]) -> dict[int, dict[str, Any]]:
    result: dict[int, dict[str, Any]] = {}
    for state in metadata.get("states", []):
        value = _value(state["value"])
        resources = []
        for resource in state.get("resources", []):
            normalized = dict(resource)
            for field in ("selector_function", "loader_function", "source", "destination"):
                if field in normalized:
                    normalized[field] = _hex(_value(normalized[field]))
            if "selector_functions" in normalized:
                normalized["selector_functions"] = [
                    _hex(_value(value))
                    for value in normalized["selector_functions"]
                ]
            resources.append(normalized)
        result[value] = {
            "label": state.get("label"),
            "confidence": state.get("confidence"),
            "resources": resources,
        }
    return result


def _state_for_event(
    state_events: list[dict[str, Any]],
    event: dict[str, Any],
    machine_frame_offset: int = 0,
) -> dict[str, Any] | None:
    scenario = event.get("scenario")
    frame = int(event.get("global_frame", event.get("frame", 0))) - machine_frame_offset
    candidates = [
        state
        for state in state_events
        if state.get("scenario") == scenario
        and int(state.get("frame", 0)) <= frame
    ]
    if not scenario:
        candidates = [
            state
            for state in state_events
            if int(state.get("frame", 0)) <= frame
        ]
    return max(candidates, key=lambda state: int(state.get("frame", 0))) if candidates else None


def _matches(
    event: dict[str, Any],
    state_entry: dict[str, Any] | None,
) -> list[dict[str, Any]]:
    if state_entry is None:
        return []
    source = event.get("source")
    destination = event.get("destination")
    function = event.get("static_loader_function")
    payload_matches = []
    function_matches = []
    for resource in state_entry.get("resources", []):
        if resource.get("source") != source or resource.get("destination") != destination:
            continue
        payload_matches.append(resource)
        if not function or resource.get("loader_function") == function:
            function_matches.append(resource)
    # Prefer the exact wrapper match, but accept a source/destination match
    # when the static function inventory names the enclosing dispatcher
    # instead of the short scene-loader wrapper.
    return function_matches or payload_matches


def analyze_scene_state_trace(
    trace_dir: Path,
    load_trace_path: Path,
    metadata_path: Path,
    output_path: Path | None = None,
    machine_frame_offset: int = 0,
) -> dict[str, Any]:
    records = _read_jsonl(trace_dir / "trace_boot.jsonl")
    load_trace = json.loads(load_trace_path.read_text(encoding="utf-8"))
    metadata = load_yaml(metadata_path) or {}
    state_events = [record for record in records if record.get("type") == "scene_state"]
    loader_events = load_trace.get("events", [])
    states = _resource_map(metadata)
    transitions = []
    for event in state_events:
        value = _value(event["value"])
        entry = states.get(value, {})
        transitions.append({
            **event,
            "value": _hex(value),
            "label": entry.get("label"),
            "mapping_confidence": entry.get("confidence"),
        })

    correlated = []
    by_state: dict[str, dict[str, Any]] = {}
    for event in loader_events:
        runtime_frame = int(event.get("global_frame", event.get("frame", 0))) - machine_frame_offset
        state_event = _state_for_event(state_events, event, machine_frame_offset)
        state_value = _value(state_event["value"]) if state_event else None
        state_entry = states.get(state_value) if state_value is not None else None
        matches = _matches(event, state_entry)
        row = {
            **event,
            "trace_frame": runtime_frame,
            "scene_state": _hex(state_value) if state_value is not None else None,
            "scene_label": state_entry.get("label") if state_entry else None,
            "scene_state_confidence": state_entry.get("confidence") if state_entry else None,
            "scene_resource_match": bool(matches),
            "scene_resource_matches": matches,
        }
        correlated.append(row)
        state_key = row["scene_state"] or "unknown"
        summary = by_state.setdefault(state_key, {
            "label": row["scene_label"],
            "loader_events": 0,
            "matched_events": 0,
            "sources": set(),
        })
        summary["loader_events"] += 1
        summary["matched_events"] += bool(matches)
        summary["sources"].add(event.get("source"))

    by_state_json = {}
    for state, summary in sorted(by_state.items()):
        by_state_json[state] = {
            **summary,
            "sources": sorted(value for value in summary["sources"] if value),
        }
    report = {
        "format": "openaladdin-scene-state-runtime-v1",
        "trace": str(trace_dir),
        "load_trace": str(load_trace_path),
        "metadata": str(metadata_path),
        "frame_alignment": {
            "machine_frame_offset": machine_frame_offset,
            "description": "Subtract this machine-frame offset from loader events before correlating with trace_boot.jsonl frames.",
        },
        "dispatcher": metadata.get("dispatcher", {}),
        "summary": {
            "scene_state_event_count": len(state_events),
            "observed_state_count": len({event.get("value") for event in state_events}),
            "loader_event_count": len(loader_events),
            "attributed_loader_event_count": sum(bool(event["scene_state"]) for event in correlated),
            "scene_resource_match_count": sum(event["scene_resource_match"] for event in correlated),
            "loader_events_without_scene_resource_match": sum(
                not event["scene_resource_match"] for event in correlated
            ),
        },
        "transitions": transitions,
        "by_state": by_state_json,
        "loader_events": correlated,
    }
    if output_path is not None:
        write_json(output_path, report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--load-trace", type=Path, required=True)
    parser.add_argument(
        "--metadata",
        type=Path,
        default=ROOT / "re/assets/scene_resources.yml",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "build/re/scene_state_runtime.json",
    )
    parser.add_argument(
        "--machine-frame-offset",
        type=int,
        default=0,
        help="subtract this MAME machine-frame offset from loader events when matching trace frames",
    )
    args = parser.parse_args()
    report = analyze_scene_state_trace(
        args.trace.resolve(),
        args.load_trace.resolve(),
        args.metadata.resolve(),
        args.output.resolve(),
        args.machine_frame_offset,
    )
    for key, value in report["summary"].items():
        print(f"{key.replace('_', ' ')}: {value}")
    print(f"scene state report: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
