#!/usr/bin/env python3
"""Smoke-test dispatch-table coverage gap reporting."""

from __future__ import annotations

import tempfile
from pathlib import Path
import struct

from openaladdin.mame.coverage_gaps import build_gap_report


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        rom = root / "rom.bin"
        data = bytearray(0x5000)
        # The test report observes one terrain entry and one VM entry.
        struct.pack_into(">I", data, 0x4554, 0x00001000)
        struct.pack_into(">I", data, 0x4954, 0x00002000)
        rom.write_bytes(data)
        coverage = {
            "format": "openaladdin-runtime-coverage-v2",
            "rom_sha256": "",
            "edges": [
                {"target": "0x001000", "tables": ["terrain"], "scenarios": ["flat"]},
                {"target": "0x002000", "tables": ["actor_vm"], "scenarios": ["run"]},
            ],
        }
        report = build_gap_report(
            coverage,
            rom,
            table_specs=(("terrain", "TERRAIN", 1), ("actor_vm", "VM", 1)),
            symbol_addresses={"TERRAIN": 0x4554, "VM": 0x4954},
        )
        assert report["summary"]["valid_entry_count"] == 2
        assert report["summary"]["covered_entry_count"] == 2
        assert report["summary"]["uncovered_entry_count"] == 0
    print("coverage gaps: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
