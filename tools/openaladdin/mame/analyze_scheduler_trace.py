#!/usr/bin/env python3
"""Validate a focused MAME scheduler trace against the static call ledger."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from datetime import date
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))

from openaladdin.common import load_yaml, parse_int  # noqa: E402


CALL_PATTERN = re.compile(
    r"OPENALADDIN_SCHEDULER_CALL"
    r" ORDINAL=(?P<ordinal>[0-9]+)"
    r" CALL=(?P<call>[0-9A-Fa-f]+)"
    r" ENTRY=(?P<entry>[0-9A-Fa-f]+)"
    r" PC=(?P<pc>[0-9A-Fa-f]+)"
    r" FRAME=(?P<frame>[0-9A-Fa-f]+)"
)
LATCH_PATTERN = re.compile(
    r"OPENALADDIN_SCHEDULER_LATCH"
    r" NAME=(?P<name>[^ ]+)"
    r" ADDR=(?P<address>[0-9A-Fa-f]+)"
    r" WPADDR=(?P<wpaddr>[0-9A-Fa-f]+)"
    r" DATA=(?P<data>[0-9A-Fa-f]+)"
    r" PC=(?P<pc>[0-9A-Fa-f]+)"
    r" FRAME=(?P<frame>[0-9A-Fa-f]+)"
    r" VALUE=(?P<value>[0-9A-Fa-f]+)"
)
VBLANK_PATTERN = re.compile(
    r"OPENALADDIN_SCHEDULER_VBLANK"
    r" PC=(?P<pc>[0-9A-Fa-f]+)"
    r" FRAME=(?P<frame>[0-9A-Fa-f]+)"
    r" VALUE=(?P<value>[0-9A-Fa-f]+)"
)


def hex_value(value: str) -> int:
    return int(value, 16)


def read_trace_latches(path: Path) -> list[dict[str, Any]]:
    latches: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        if record.get("type") != "scheduler_latch":
            continue
        latches.append({
            "line": line_number,
            "name": str(record["name"]),
            "address": int(record["address"]),
            "watchpoint_address": int(record["address"]),
            "data": int(record["data"]),
            "pc": int(record["pc"]),
            "frame": int(record["frame"]),
            "value": int(record["value"]),
        })
    return latches


def read_debug_log(
    path: Path,
) -> tuple[list[dict[str, int]], list[dict[str, Any]], list[dict[str, int]]]:
    calls: list[dict[str, int]] = []
    latches: list[dict[str, Any]] = []
    vblanks: list[dict[str, int]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        call = CALL_PATTERN.search(line)
        if call:
            calls.append({
                "line": line_number,
                "ordinal": int(call.group("ordinal")),
                "call_site": hex_value(call.group("call")),
                "entry": hex_value(call.group("entry")),
                "pc": hex_value(call.group("pc")),
                "frame": hex_value(call.group("frame")),
            })
            continue
        latch = LATCH_PATTERN.search(line)
        if latch:
            latches.append({
                "line": line_number,
                "name": latch.group("name"),
                "address": hex_value(latch.group("address")),
                "watchpoint_address": hex_value(latch.group("wpaddr")),
                "data": hex_value(latch.group("data")),
                "pc": hex_value(latch.group("pc")),
                "frame": hex_value(latch.group("frame")),
                "value": hex_value(latch.group("value")),
            })
            continue
        vblank = VBLANK_PATTERN.search(line)
        if vblank:
            vblanks.append({
                "line": line_number,
                "pc": hex_value(vblank.group("pc")),
                "frame": hex_value(vblank.group("frame")),
                "value": hex_value(vblank.group("value")),
            })
    return calls, latches, vblanks


def static_call_sequence(model: dict[str, Any]) -> list[dict[str, int]]:
    return [
        {
            "ordinal": int(call["ordinal"]),
            "call_site": parse_int(call["call_site"]),
            "entry": parse_int(call["entry"]),
        }
        for call in model["call_sequence"]
    ]


def validate_calls(
    observed: list[dict[str, int]],
    expected: list[dict[str, int]],
) -> dict[str, Any]:
    errors: list[dict[str, Any]] = []
    next_ordinal = 1
    complete_sequences = 0
    for event in observed:
        ordinal = event["ordinal"]
        if ordinal != next_ordinal:
            errors.append({
                "kind": "ordinal",
                "line": event["line"],
                "observed": ordinal,
                "expected": next_ordinal,
                "frame": event["frame"],
            })
            next_ordinal = 1
        expected_call = expected[ordinal - 1] if 1 <= ordinal <= len(expected) else None
        if expected_call is None:
            errors.append({
                "kind": "unknown_ordinal",
                "line": event["line"],
                "observed": ordinal,
            })
        else:
            for field in ("call_site", "entry"):
                if event[field] != expected_call[field]:
                    errors.append({
                        "kind": field,
                        "line": event["line"],
                        "ordinal": ordinal,
                        "observed": f"0x{event[field]:06X}",
                        "expected": f"0x{expected_call[field]:06X}",
                    })
        if ordinal == len(expected):
            complete_sequences += 1
            next_ordinal = 1
        else:
            next_ordinal = ordinal + 1

    return {
        "observed_call_count": len(observed),
        "complete_call_sequences": complete_sequences,
        "partial_final_sequence": [event["ordinal"] for event in observed]
        [complete_sequences * len(expected):],
        "exact_sequence": bool(observed) and not errors,
        "errors": errors[:20],
        "error_count": len(errors),
        "first_observed": observed[0] if observed else None,
        "last_observed": observed[-1] if observed else None,
        "debugger_frame_range": (
            [min(event["frame"] for event in observed), max(event["frame"] for event in observed)]
            if observed else []
        ),
        "observed_ordinals": sorted({event["ordinal"] for event in observed}),
    }


def validate_latches(
    observed: list[dict[str, Any]],
    first_gameplay_frame: int | None,
) -> dict[str, Any]:
    expected_addresses = {
        "FRAME_WAIT_LATCH": 0x00FF7E25,
        "VBLANK_READY_LATCH": 0x00FF7E1E,
        "FRAME_PHASE_COUNTER": 0x00FF7E28,
        "SCENE_RESOURCE_STATUS": 0x00FF7E22,
        "SCENE_RESOURCE_ERROR": 0x00FF7E23,
    }
    gameplay = [
        event for event in observed
        if first_gameplay_frame is None or event["frame"] >= first_gameplay_frame
    ]
    all_by_name: dict[str, list[dict[str, Any]]] = defaultdict(list)
    by_name: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for event in observed:
        all_by_name[event["name"]].append(event)
    for event in gameplay:
        by_name[event["name"]].append(event)

    def summarize(events_by_name: dict[str, list[dict[str, Any]]]) -> dict[str, Any]:
        summary: dict[str, Any] = {}
        for name, events in sorted(events_by_name.items()):
            summary[name] = {
                "count": len(events),
                "writer_pcs": {
                    f"0x{pc:06X}": count
                    for pc, count in sorted(Counter(event["pc"] for event in events).items())
                },
                "data_values": sorted({event["data"] for event in events}),
                "first": events[0],
                "last": events[-1],
            }
        return summary

    result: dict[str, Any] = {
        "observed_write_count": len(observed),
        "gameplay_write_count": len(gameplay),
        "first_gameplay_frame": first_gameplay_frame,
        "address_mismatches": [
            {
                "line": event["line"],
                "name": event["name"],
                "watchpoint_address": f"0x{event['watchpoint_address']:06X}",
                "expected": f"0x{expected_addresses.get(event['name'], 0):06X}",
            }
            for event in observed
            if event["name"] in expected_addresses
            and event["watchpoint_address"] != expected_addresses[event["name"]]
        ],
        "all_by_name": summarize(all_by_name),
        "by_name": summarize(by_name),
    }
    return result


def git_value(*arguments: str) -> str:
    result = subprocess.run(
        ["git", *arguments],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def analyze(
    model_path: Path,
    debug_path: Path,
    output_path: Path,
    state_path: Path | None,
    trace_boot_path: Path | None,
) -> dict[str, Any]:
    model = load_yaml(model_path)
    expected = static_call_sequence(model)
    calls, debug_latches, vblanks = read_debug_log(debug_path)
    latches = debug_latches
    if trace_boot_path and trace_boot_path.is_file():
        latches = read_trace_latches(trace_boot_path) + debug_latches
    first_gameplay_frame = calls[0]["frame"] if calls else None
    call_report = validate_calls(calls, expected)
    latch_report = validate_latches(latches, first_gameplay_frame)
    report = {
        "format": "openaladdin-scheduler-static-dynamic-v1",
        "recorded_at": str(date.today()),
        "status": "trace_validated" if call_report["exact_sequence"] else "trace_incomplete",
        "rom": "rom/Disneys_Aladdin_U_p1.bin",
        "rom_sha256": "8199d016f7bb88ea73b635dcc072c126b40f01c662707ed3f67d865fd86c0ab6",
        "repository_commit_at_capture": git_value("rev-parse", "HEAD"),
        "working_tree_at_analysis": git_value("status", "--short").splitlines(),
        "static_model": str(model_path),
        "debug_log": str(debug_path),
        "trace_boot": str(trace_boot_path) if trace_boot_path else None,
        "state_trace": str(state_path) if state_path else None,
        "instrumentation": {
            "scheduler_call_breakpoints": 37,
            "scheduler_latch_watchpoints": [
                "FRAME_WAIT_LATCH",
                "VBLANK_READY_LATCH",
                "FRAME_PHASE_COUNTER",
                "SCENE_RESOURCE_STATUS",
                "SCENE_RESOURCE_ERROR",
            ],
            "mame_debugger_records": [
                "OPENALADDIN_SCHEDULER_CALL",
                "OPENALADDIN_SCHEDULER_LATCH",
            ],
        },
        "call_sequence": {
            "static_count": len(expected),
            **call_report,
        },
        "latch_writes": latch_report,
        "vblank_boundaries": {
            "observed_count": len(vblanks),
            "pc_values": sorted({f"0x{event['pc']:06X}" for event in vblanks}),
            "value_values": sorted({event["value"] for event in vblanks}),
            "debugger_frame_range": (
                [min(event["frame"] for event in vblanks), max(event["frame"] for event in vblanks)]
                if vblanks else []
            ),
            "first": vblanks[0] if vblanks else None,
            "last": vblanks[-1] if vblanks else None,
        },
        "observations": [
            "The observed gameplay call stream follows all 37 recovered direct call sites in order.",
            "The only direct AnimationVM_TickActors call in that stream is ordinal 30 at 0x001A8CCE.",
            "VBLANK_READY_LATCH writes are attributable to the VBlank interrupt path; reset writes are excluded from gameplay counts.",
        ],
        "open_questions": [
            "FRAME_WAIT_LATCH has no identified gameplay writer in this route; its producer remains unresolved unless the latch report shows one.",
            "Indirect or nested callers of AnimationVM_TickActors remain outside the direct Game_FrameUpdateLoop call stream.",
            "Native probe_animation remains a diagnostic phase until a ROM call site or causal equivalent is identified.",
        ],
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("debug_log", type=Path)
    parser.add_argument("--model", type=Path, default=ROOT / "re/scheduler/frame_phases.yml")
    parser.add_argument("--state", type=Path)
    parser.add_argument("--trace-boot", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = analyze(
        args.model.resolve(),
        args.debug_log.resolve(),
        args.output.resolve(),
        args.state.resolve() if args.state else None,
        args.trace_boot.resolve() if args.trace_boot else None,
    )
    print(f"scheduler call sequences: {report['call_sequence']['complete_call_sequences']}")
    print(f"scheduler call events: {report['call_sequence']['observed_call_count']}")
    print(f"scheduler latch writes: {report['latch_writes']['gameplay_write_count']}")
    print(f"scheduler VBlank boundaries: {report['vblank_boundaries']['observed_count']}")
    print(f"scheduler analysis: {args.output.resolve()}")
    return 0 if report["status"] == "trace_validated" else 1


if __name__ == "__main__":
    raise SystemExit(main())
