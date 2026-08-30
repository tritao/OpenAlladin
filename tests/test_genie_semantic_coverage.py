from __future__ import annotations

import json
from pathlib import Path

from genie.cli import build_parser
from genie.data import DataIndex
from genie.ghidra.database import AnalysisDatabase
from genie.layout.model import Layout, LayoutRange
from genie.semantic_coverage import build_semantic_coverage
from genie.symbols import Symbol, SymbolStore


def _database(root: Path) -> Path:
    database = root / "full-rom"
    database.mkdir(parents=True)
    documents = {
        "metadata.json": {"format": "test", "rom_size": 0x80},
        "functions.json": [
            {"address": "0x10", "start": "0x10", "end": "0x1F", "name": "SemanticFunction"},
            {"address": "0x20", "start": "0x20", "end": "0x2F", "name": "Func_00000020"},
        ],
        "xrefs.json": {"references": [
            {
                "from": "0x10",
                "from_function": "0x10",
                "from_function_name": "SemanticFunction",
                "to": "0x40",
                "type": "DATA",
            },
        ]},
        "memory_reads.json": {"references": [{"from": "0x10", "to": "0xFF0000", "type": "READ"}]},
        "memory_writes.json": {"references": []},
        "callgraph.json": {"edges": []},
        "indirect_calls.json": {"references": []},
        "jump_tables.json": {"tables": []},
        "address_classes.json": {"classes": []},
        "instructions.json": [],
    }
    for filename, value in documents.items():
        (database / filename).write_text(json.dumps(value), encoding="utf-8")
    (database / "decompile").mkdir()
    (database / "decompile/00000010.txt").write_text("void SemanticFunction(void) {}\n", encoding="utf-8")
    return database


def test_semantic_coverage_aggregates_functions_objects_and_integrity(tmp_path):
    database_root = _database(tmp_path)
    layout = Layout(
        rom_size=0x80,
        ranges=(
            LayoutRange(0x00, 0x3F, "CODE", "test"),
            LayoutRange(0x40, 0x43, "ANIMATION_STREAM", "tracked.symbol", "AnimationRoot"),
            LayoutRange(0x44, 0x4F, "UNKNOWN", "layout.gap"),
            LayoutRange(0x50, 0x63, "ACTOR_TEMPLATE", "tracked.symbol", "ActorTemplate"),
            LayoutRange(0x64, 0x7F, "UNKNOWN", "layout.gap"),
        ),
    )
    symbols = SymbolStore(symbols=(
        Symbol(0x10, "SemanticFunction", "function", confidence="decompiled"),
        Symbol(0x20, "Func_00000020", "function"),
        Symbol(0x40, "AnimationRoot", "data", confidence="confirmed", size=4),
        Symbol(0x50, "ActorTemplate", "data", confidence="confirmed", size=20, metadata={"type": "actor_template"}),
        Symbol(0xFF0000, "RAM_FIELD", "ram", confidence="confirmed", metadata={"type": "u8"}),
    ))
    animation = tmp_path / "animation.json"
    animation.write_text(json.dumps({"streams": {
        "AnimationRoot": {
            "entry": "0x40",
            "bytes_decoded": 4,
            "stopped_reason": "unconditional_jump",
            "instructions": [{"address": "0x40", "size": 4}],
        },
    }}), encoding="utf-8")
    coverage = tmp_path / "coverage.json"
    coverage.write_text(json.dumps({"functions": {
        "0x10": {"pc_count": 3, "scenarios": ["smoke"]},
    }}), encoding="utf-8")
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=layout,
        coverage_path=coverage,
        animation_path=animation,
    )

    report = build_semantic_coverage(
        index.database,
        data_index=index,
        symbols=symbols,
        coverage_path=coverage,
        root=tmp_path,
    )
    assert report["format"] == "openaladdin-semantic-coverage-v1"
    assert report["functions"]["discovered"] == 2
    assert report["functions"]["semantic"] == 1
    assert report["functions"]["mechanical"] == 1
    assert report["functions"]["decompiled"] == 1
    assert report["functions"]["runtime_observed"] == 1
    assert report["rom_objects"]["by_kind"]["animation"]["decoded"] == 1
    assert report["rom_objects"]["by_kind"]["animation"]["with_consumers"] == 1
    assert report["rom_objects"]["by_kind"]["actor-template"]["typed"] == 1
    assert report["ram"]["referenced"] == 1
    assert report["integrity"]["unowned_rom_ranges"]["count"] == 2
    assert report["integrity"]["unknown_pointer_targets"]["count"] == 0


def test_semantic_coverage_cli_surface_dispatches():
    args = build_parser().parse_args(["coverage", "report", "--json"])
    assert args.function.__name__ == "command_coverage_report"
    assert args.json_output is True


def test_semantic_coverage_aggregates_merged_per_pc_archive(tmp_path):
    database_root = _database(tmp_path)
    symbols = SymbolStore(symbols=(
        Symbol(0x10, "SemanticFunction", "function", confidence="decompiled"),
        Symbol(0x20, "Func_00000020", "function"),
    ))
    coverage = tmp_path / "coverage-expanded.json"
    coverage.write_text(json.dumps({"pcs": {
        "0x00000012": {"sample_count": 4, "scenarios": ["first", "second"]},
        "0x00000020": {"sample_count": 2, "scenarios": ["second"]},
    }}), encoding="utf-8")
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        coverage_path=coverage,
    )

    report = build_semantic_coverage(
        index.database,
        data_index=index,
        symbols=symbols,
        coverage_path=coverage,
        root=tmp_path,
    )

    assert report["functions"]["runtime_observed"] == 2
    assert report["functions"]["runtime_pc_count"] == 2
