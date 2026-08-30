from __future__ import annotations

import json
from pathlib import Path

from genie.cli import build_parser
from genie.core.mame.rom_reads import summarize_rom_reads
from genie.ghidra.database import AnalysisDatabase
from genie.symbols import Symbol, SymbolStore


def _trace(root: Path) -> Path:
    trace = root / "scenario"
    trace.mkdir()
    records = [
        {"type": "header", "rom_sha256": "rom"},
        {"type": "frame", "frame": 0, "pc": 0x10},
        {
            "type": "rom_read",
            "frame": 4,
            "address": 0x42,
            "range_start": 0x40,
            "range_end": 0x43,
            "pc": 0x14,
        },
    ]
    (trace / "trace_boot.jsonl").write_text(
        "".join(json.dumps(record) + "\n" for record in records),
        encoding="utf-8",
    )
    return trace


def _database(root: Path) -> AnalysisDatabase:
    database = root / "full-rom"
    database.mkdir()
    (database / "metadata.json").write_text(json.dumps({"format": "test"}), encoding="utf-8")
    (database / "functions.json").write_text(json.dumps([
        {"address": "0x10", "start": "0x10", "end": "0x1F", "name": "ReadCaller"},
    ]), encoding="utf-8")
    return AnalysisDatabase(database)


def test_rom_read_summary_maps_symbol_and_containing_function(tmp_path):
    report = summarize_rom_reads(
        [_trace(tmp_path)],
        database=_database(tmp_path),
        symbols=SymbolStore(symbols=(Symbol(0x40, "PALETTE_SOURCE", "data", size=4),)),
        trace_root=tmp_path,
    )

    assert report["summary"] == {
        "scenario_count": 1,
        "target_count": 1,
        "read_count": 1,
        "consumer_count": 1,
    }
    target = report["targets"][0]
    assert target["symbol"]["name"] == "PALETTE_SOURCE"
    assert target["addresses"] == ["0x00000042"]
    assert target["consumers"][0]["function_name"] == "ReadCaller"
    assert target["consumers"][0]["pc"] == "0x00000014"


def test_coverage_reads_cli_surface_dispatches():
    args = build_parser().parse_args(["coverage", "reads", "--json"])
    assert args.function.__name__ == "command_coverage_reads"
    assert args.json_output is True
