"""Validate tracked scene/resource mappings against static RNC loader calls."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from openaladdin.common import load_yaml, parse_int, write_json


def _hex(value: int) -> str:
    return f"0x{value:06X}"


def _value(value: Any) -> int:
    return parse_int(value)


def validate_scene_resources(
    metadata_path: Path,
    loader_path: Path,
    output_path: Path | None = None,
) -> dict[str, Any]:
    metadata = load_yaml(metadata_path) or {}
    loader = json.loads(loader_path.read_text(encoding="utf-8"))
    calls = loader.get("calls", [])
    index: dict[tuple[int, int, int], list[dict[str, Any]]] = {}
    payload_index: dict[tuple[int, int], list[dict[str, Any]]] = {}
    for call in calls:
        function = call.get("function")
        source = call.get("source")
        destination = call.get("destination")
        if not function or not source or not destination:
            continue
        key = (
            _value(function["address"]),
            _value(source["target"]),
            _value(destination["target"]),
        )
        index.setdefault(key, []).append(call)
        payload_index.setdefault(key[1:], []).append(call)

    states = []
    errors = []
    resource_count = 0
    matched_count = 0
    for state in metadata.get("states", []):
        state_value = _value(state["value"])
        rows = []
        for resource in state.get("resources", []):
            resource_count += 1
            key = (
                _value(resource["loader_function"]),
                _value(resource["source"]),
                _value(resource["destination"]),
            )
            matches = index.get(key, [])
            match_mode = "function_source_destination"
            if not matches:
                # The static function inventory can collapse short scene
                # loader wrappers into their enclosing dispatcher.  The ROM
                # source/destination pair remains unambiguous and is the
                # stronger asset identity for this validation.
                matches = payload_index.get(key[1:], [])
                match_mode = "source_destination"
            if not matches:
                errors.append({
                    "state": _hex(state_value),
                    "resource": resource,
                    "error": "no_static_loader_match",
                })
                continue
            matched_count += 1
            rows.append({
                **resource,
                "loader_function": _hex(key[0]),
                "source": _hex(key[1]),
                "destination": _hex(key[2]),
                "static_match_mode": match_mode,
                "static_function_match": match_mode == "function_source_destination",
                "static_call_addresses": [call["call_address"] for call in matches],
                "static_block": matches[0].get("block"),
            })
        states.append({
            "value": _hex(state_value),
            "label": state.get("label"),
            "confidence": state.get("confidence"),
            "resources": rows,
        })

    report = {
        "format": "openaladdin-scene-resources-report-v1",
        "metadata": str(metadata_path),
        "loader_analysis": str(loader_path),
        "rom": loader.get("rom", {}),
        "dispatcher": metadata.get("dispatcher", {}),
        "post_load": metadata.get("post_load", {}),
        "states": states,
        "unmapped_states": [_hex(_value(value)) for value in metadata.get("unmapped_states", [])],
        "summary": {
            "state_count": len(states),
            "resource_count": resource_count,
            "matched_resource_count": matched_count,
            "exact_function_match_count": sum(
                1
                for state_row in states
                for resource_row in state_row["resources"]
                if resource_row.get("static_function_match")
            ),
            "payload_fallback_match_count": sum(
                1
                for state_row in states
                for resource_row in state_row["resources"]
                if resource_row.get("static_match_mode") == "source_destination"
            ),
            "error_count": len(errors),
        },
        "errors": errors,
    }
    if output_path is not None:
        write_json(output_path, report)
    return report
