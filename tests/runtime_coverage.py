#!/usr/bin/env python3
"""Smoke-test deterministic runtime-PC coverage merging."""

from __future__ import annotations

import json
from pathlib import Path
import tempfile

from openaladdin.mame.coverage import merge_coverage


def write_trace(path: Path, frames: list[tuple[int, int]], sha256: str = "rom") -> None:
    path.mkdir(parents=True)
    records = [{"type": "header", "rom_sha256": sha256}]
    records.extend({"type": "frame", "frame": frame, "pc": pc} for frame, pc in frames)
    (path / "trace_boot.jsonl").write_text(
        "".join(json.dumps(record) + "\n" for record in records),
        encoding="utf-8",
    )


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        first = root / "first"
        second = root / "second"
        output = root / "coverage.json"
        write_trace(first, [(0, 0), (1, 0x100), (2, 0x200)])
        write_trace(second, [(0, 0x200), (1, 0x300)])

        report = merge_coverage([first, second], output, trace_root=root)
        assert report["format"] == "openaladdin-runtime-coverage-v1"
        assert report["summary"]["zero_pc_frames"] == 1
        assert report["summary"]["unique_pc_count"] == 3
        assert report["pcs"]["0x000200"]["scenarios"] == ["first", "second"]
        assert json.loads(output.read_text(encoding="utf-8"))["summary"] == report["summary"]
    print("runtime coverage: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
