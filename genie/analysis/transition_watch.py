#!/usr/bin/env python3
"""Summarize MAME debugger writes for the level-transition state path."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from genie.common import ROOT, parse_int, write_json


WRITE_RE = re.compile(
    r"OPENALADDIN_WRITE\s+PC=(?P<pc>[0-9A-Fa-f]+)\s+"
    r"ADDR=(?P<address>[0-9A-Fa-f]+)\s+DATA=(?P<data>[0-9A-Fa-f]+)"
    r"(?:\s+FRAME=(?P<frame>[0-9A-Fa-f]+))?"
)


def _hex(value: int, width: int = 6) -> str:
    return f"0x{value:0{width}X}"


def _read_writes(path: Path) -> list[dict[str, Any]]:
    writes = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = WRITE_RE.search(line)
        if not match:
            continue
        row = {
            "line": line_number,
            "pc": _hex(int(match.group("pc"), 16)),
            "address": _hex(int(match.group("address"), 16)),
            "data": _hex(int(match.group("data"), 16), 8),
        }
        if match.group("frame") is not None:
            row["frame"] = int(match.group("frame"), 16)
        writes.append(row)
    return writes


def analyze_transition_watch(
    log_path: Path,
    state_address: int,
    output_path: Path | None = None,
) -> dict[str, Any]:
    writes = _read_writes(log_path)
    state = _hex(state_address)
    state_writes = [row for row in writes if int(row["address"], 16) == state_address]
    state_values = sorted({row["data"] for row in state_writes})
    writers = Counter(row["pc"] for row in state_writes)
    addresses = Counter(row["address"] for row in writes)
    report = {
        "format": "openaladdin-transition-watch-v1",
        "log": str(log_path),
        "state_address": state,
        "writes": writes,
        "state_writes": state_writes,
        "summary": {
            "write_count": len(writes),
            "state_write_count": len(state_writes),
            "state_values": state_values,
            "state_writer_pcs": [
                {"pc": pc, "count": count}
                for pc, count in sorted(writers.items())
            ],
            "write_counts_by_address": [
                {"address": address, "count": count}
                for address, count in sorted(addresses.items())
            ],
        },
    }
    if output_path is not None:
        write_json(output_path, report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--state-address", default="0xFF7E26")
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "build/re/level-transition-watch/transition_watch.json",
    )
    args = parser.parse_args()
    report = analyze_transition_watch(
        args.log.resolve(),
        parse_int(args.state_address),
        args.output.resolve(),
    )
    for key, value in report["summary"].items():
        print(f"{key.replace('_', ' ')}: {value}")
    print(f"transition watch report: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
