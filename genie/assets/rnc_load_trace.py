#!/usr/bin/env python3
"""Map MAME RNC-loader breakpoint events to static ROM asset records."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
from typing import Any

from genie.common import ROOT


EVENT_RE = re.compile(
    r"OPENALADDIN_RNC_LOAD"
    r"\s+PC=(?P<pc>[0-9A-Fa-f]+)"
    r"\s+RETURN=(?P<return>[0-9A-Fa-f]+)"
    r"\s+SOURCE=(?P<source>[0-9A-Fa-f]+)"
    r"\s+DEST=(?P<destination>[0-9A-Fa-f]+)"
    r"\s+FRAME=(?P<frame>[0-9A-Fa-f]+)"
)


def _hex(value: int) -> str:
    return f"0x{value:06X}"


def parse_events(path: Path) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
        match = EVENT_RE.search(line)
        if not match:
            continue
        values = {name: int(value, 16) for name, value in match.groupdict().items()}
        events.append({
            "line": line_number,
            "loader_pc": _hex(values["pc"]),
            "return_address": _hex(values["return"]),
            "call_address": _hex(values["return"] - 4),
            "source": _hex(values["source"]),
            "destination": _hex(values["destination"]),
            "frame": values["frame"],
        })
    return events


def analyze_load_trace(log_path: Path, loader_path: Path) -> dict[str, Any]:
    loader = json.loads(loader_path.read_text(encoding="utf-8"))
    events = parse_events(log_path)
    static_calls = {
        call["call_address"]: call
        for call in loader.get("calls", [])
    }
    corpus_by_offset = {
        call["block"]["offset"]: call["block"]
        for call in loader.get("calls", [])
        if call.get("block")
    }
    rows: list[dict[str, Any]] = []
    for event in events:
        static = static_calls.get(event["call_address"])
        source_block = corpus_by_offset.get(event["source"])
        row = {
            **event,
            "static_call_match": static is not None,
            "static_loader_function": static.get("function", {}).get("address") if static else None,
            "static_loader_name": static.get("function", {}).get("name") if static else None,
            "static_source": static.get("source", {}).get("target") if static else None,
            "static_destination": static.get("destination", {}).get("target") if static else None,
            "block": source_block,
            "source_matches_static": bool(static and static.get("source", {}).get("target") == event["source"]),
            "destination_matches_static": bool(static and static.get("destination", {}).get("target") == event["destination"]),
        }
        rows.append(row)

    blocks = sorted({row["source"] for row in rows if row.get("block")}, key=lambda value: int(value, 16))
    destinations: dict[str, int] = {}
    for row in rows:
        destinations[row["destination"]] = destinations.get(row["destination"], 0) + 1
    report = {
        "format": "openaladdin-rnc-load-trace-v1",
        "rom": loader.get("rom", {}),
        "log": str(log_path),
        "loader_analysis": str(loader_path),
        "summary": {
            "event_count": len(rows),
            "unique_call_count": len({row["call_address"] for row in rows}),
            "unique_source_count": len({row["source"] for row in rows}),
            "known_block_count": len(blocks),
            "unknown_source_count": sum(row.get("block") is None for row in rows),
            "static_call_match_count": sum(row["static_call_match"] for row in rows),
            "static_source_match_count": sum(row["source_matches_static"] for row in rows),
            "static_destination_match_count": sum(row["destination_matches_static"] for row in rows),
            "by_destination": dict(sorted(destinations.items())),
        },
        "events": rows,
        "observed_blocks": blocks,
    }
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, default=ROOT / "debug.log")
    parser.add_argument("--loader", type=Path, default=ROOT / "build/assets/rnc/loader_analysis.json")
    parser.add_argument("--output", type=Path, default=ROOT / "build/re/rnc_loads.json")
    args = parser.parse_args()

    log_path = args.log.resolve()
    loader_path = args.loader.resolve()
    if not log_path.is_file():
        raise SystemExit(f"debug log not found: {log_path}")
    if not loader_path.is_file():
        raise SystemExit(f"loader analysis not found: {loader_path}")
    report = analyze_load_trace(log_path, loader_path)
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for key, value in report["summary"].items():
        if key != "by_destination":
            print(f"{key.replace('_', ' ')}: {value}")
    print(f"load trace report: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
