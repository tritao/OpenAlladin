#!/usr/bin/env python3
"""Smoke-test deterministic runtime-PC coverage merging."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from genie.core.mame.coverage import merge_coverage


def write_trace(path: Path, frames: list[tuple[int, int]], sha256: str = "rom", *, edge: bool = False) -> None:
    path.mkdir(parents=True)
    records = [{"type": "header", "rom_sha256": sha256}]
    records.extend({"type": "frame", "frame": frame, "pc": pc} for frame, pc in frames)
    (path / "trace_boot.jsonl").write_text(
        "".join(json.dumps(record) + "\n" for record in records),
        encoding="utf-8",
    )
    if edge:
        (path / "debug.log").write_text(
            "OPENALADDIN_EDGE TABLE=player_collision+terrain TARGET=001B5318 RETURN=001B1E3A FRAME=00000002\n",
            encoding="utf-8",
        )


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        first = root / "first"
        second = root / "second"
        output = root / "coverage.json"
        write_trace(first, [(0, 0), (1, 0x100), (2, 0x200)])
        write_trace(second, [(0, 0x200), (1, 0x300)], edge=True)

        report = merge_coverage([first, second], output, trace_root=root)
        assert report["format"] == "openaladdin-runtime-coverage-v2"
        assert report["summary"]["zero_pc_frames"] == 1
        assert report["summary"]["unique_pc_count"] == 3
        assert report["summary"]["unique_edge_count"] == 1
        assert report["edges"][0]["source"] == "0x1B1E38"
        assert report["edges"][0]["target"] == "0x1B5318"
        assert report["edges"][0]["tables"] == ["player_collision", "terrain"]
        assert report["pcs"]["0x000200"]["scenarios"] == ["first", "second"]
        assert json.loads(output.read_text(encoding="utf-8"))["summary"] == report["summary"]
    print("runtime coverage: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
