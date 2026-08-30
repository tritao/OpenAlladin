from __future__ import annotations

import json
from pathlib import Path

from genie.cli import build_parser
from genie.ghidra.context import build_context
from genie.ghidra.database import AnalysisDatabase
from genie.ghidra.worklist import function_work_queue, symbol_review_queue
from genie.layout.model import Layout, LayoutRange
from genie.symbols import Symbol, SymbolStore, edit_symbol, mechanical_name, name_for


def _write_symbol_tree(root: Path) -> None:
    (root / "re/symbols").mkdir(parents=True)
    (root / "re/symbols/functions.yml").write_text(
        "0x10:\n"
        "  name: SemanticFunction\n"
        "  confidence: confirmed\n"
        "  evidence: [static_audit]\n"
        "  aliases: [OldFunction]\n"
        "0x20:\n"
        "  name: SecondFunction\n",
        encoding="utf-8",
    )
    (root / "re/symbols/ram.yml").write_text(
        "0xFF0000:\n"
        "  name: WORK_RAM_BASE\n"
        "  type: address\n",
        encoding="utf-8",
    )
    (root / "re/symbols/data.yml").write_text(
        "0x30:\n"
        "  name: TableStart\n"
        "  entry_size: 2\n"
        "  count: 4\n",
        encoding="utf-8",
    )


def test_symbol_store_normalizes_tracked_maps(tmp_path):
    _write_symbol_tree(tmp_path)
    store = SymbolStore(root=tmp_path)

    function = store.at(0x10)
    table = store.at(0x33)
    assert function is not None
    assert function.kind == "function"
    assert function.source == "re/symbols/functions.yml"
    assert function.provenance == ("static_audit",)
    assert function.aliases == ("OldFunction",)
    assert table is not None
    assert table.size == 8
    assert table.end == 0x37
    assert [item.name for item in store.find("oldfunction")] == ["SemanticFunction"]
    assert store.validate(rom_size=0x100) == []


def test_symbol_naming_preserves_semantic_names():
    assert mechanical_name(0x184320, "function") == "Func_00184320"
    assert mechanical_name(0x1CBE, "table") == "Table_001CBE"
    assert name_for(0x184320, "function", "Known_Name") == "Known_Name"
    assert name_for(0x184320, "function", "Func_00184320") == "Func_00184320"


def test_symbols_cli_surface_dispatches():
    show = build_parser().parse_args(["symbols", "show", "0x001AC784"])
    find = build_parser().parse_args(["symbols", "find", "AnimationVM", "--kind", "function"])
    stats = build_parser().parse_args(["symbols", "stats", "--json"])
    unknown = build_parser().parse_args(["symbols", "unknown", "--kind", "function", "--limit", "4"])
    review = build_parser().parse_args(["symbols", "review", "--kind", "data", "--limit", "4", "--json"])
    rename = build_parser().parse_args(["symbols", "rename", "0x20", "Scene_Init"])
    describe = build_parser().parse_args(["symbols", "describe", "0x20", "entry point"])
    confidence = build_parser().parse_args(["symbols", "confidence", "0x20", "decompiled"])
    assert show.address == 0x1AC784
    assert find.kind == "function"
    assert stats.json_output is True
    assert unknown.limit == 4
    assert review.kind == "data"
    assert review.limit == 4
    assert review.json_output is True
    assert rename.name == "Scene_Init"
    assert describe.description == "entry point"
    assert confidence.confidence == "decompiled"


def test_symbol_editor_promotes_and_annotates_a_tracked_symbol(tmp_path):
    _write_symbol_tree(tmp_path)

    updated = edit_symbol(
        0x10,
        root=tmp_path,
        name="Scene_Init",
        description="scene entry point",
        confidence="decompiled",
    )

    assert updated.name == "Scene_Init"
    assert updated.description == "scene entry point"
    assert updated.confidence == "decompiled"
    assert "  name: Scene_Init" in (tmp_path / "re/symbols/functions.yml").read_text()


def test_function_work_queue_ranks_runtime_unknowns(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    (tmp_path / "coverage-ghidra.json").write_text(
        json.dumps({
            "functions": {
                "0x00000020": {"pc_count": 4, "scenarios": ["smoke"]},
            },
        }),
        encoding="utf-8",
    )
    database = AnalysisDatabase(database_root)
    queue = function_work_queue(
        database,
        SymbolStore(symbols=(Symbol(0x10, "FirstFunction", "function", confidence="confirmed"),)),
    )

    assert len(queue) == 1
    assert queue[0]["address"] == "0x00000020"
    assert queue[0]["runtime_observed"] is True
    assert queue[0]["callers"] == 1


def test_function_work_queue_reads_merged_per_pc_archive(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    (tmp_path / "coverage-expanded.json").write_text(
        json.dumps({"pcs": {
            "0x00000022": {"sample_count": 3, "scenarios": ["menu", "game"]},
        }}),
        encoding="utf-8",
    )
    queue = function_work_queue(
        AnalysisDatabase(database_root),
        SymbolStore(symbols=(Symbol(0x10, "FirstFunction", "function", confidence="confirmed"),)),
        coverage_path=tmp_path / "coverage-expanded.json",
    )

    assert len(queue) == 1
    assert queue[0]["address"] == "0x00000020"
    assert queue[0]["runtime_observed"] is True
    assert queue[0]["runtime_pc_count"] == 1
    assert queue[0]["runtime_scenarios"] == ["game", "menu"]


def test_function_work_queue_includes_low_confidence_canonical_functions(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    queue = function_work_queue(
        AnalysisDatabase(database_root),
        SymbolStore(symbols=(
            Symbol(0x10, "ProbableFunction", "function", confidence="probable"),
            Symbol(0x20, "ConfirmedFunction", "function", confidence="confirmed"),
        )),
    )

    assert [item["address"] for item in queue] == ["0x00000010"]
    assert queue[0]["confidence"] == "probable"


def test_symbol_review_queue_keeps_named_open_questions_actionable(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    queue = symbol_review_queue(
        AnalysisDatabase(database_root),
        SymbolStore(symbols=(
            Symbol(
                0x10,
                "NamedOpenQuestion",
                "function",
                confidence="decompiled",
                description="The selector remains unresolved.",
            ),
            Symbol(0x20, "ClosedFunction", "function", confidence="confirmed"),
        )),
    )

    assert len(queue) == 1
    assert queue[0]["address"] == "0x00000010"
    assert queue[0]["review_markers"] == ["unresolved", "selector remains"]
    assert queue[0]["description"] == "The selector remains unresolved."


def test_context_combines_function_references_and_layout(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    layout = Layout(
        rom_size=0x40,
        ranges=(
            LayoutRange(0x00, 0x0F, "UNKNOWN", "test"),
            LayoutRange(0x10, 0x28, "CODE", "test", "FirstFunction"),
            LayoutRange(0x29, 0x3F, "OPAQUE_DATA", "test"),
        ),
    )
    (database_root / "layout.json").write_text(json.dumps(layout.to_dict()), encoding="utf-8")
    value = build_context(
        AnalysisDatabase(database_root),
        0x14,
        SymbolStore(symbols=(Symbol(0x10, "FirstFunction", "function"),)),
    )

    assert value["function"]["name"] == "First"
    assert value["layout"]["class"] == "CODE"
    assert len(value["callers"]) == 0
    assert len(value["callees"]) == 1
    assert len(value["ram_reads"]) == 1
    assert len(value["ram_writes"]) == 1
    assert value["nearby_layout"]


def test_context_can_include_cached_pseudocode(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    (database_root / "decompile").mkdir()
    (database_root / "decompile/00000010.txt").write_text(
        "void First(void) {}\n", encoding="utf-8"
    )

    value = build_context(
        AnalysisDatabase(database_root),
        0x14,
        SymbolStore(symbols=(Symbol(0x10, "FirstFunction", "function"),)),
        include_decompile=True,
    )

    assert value["decompile"]["available"] is True
    assert value["decompile"]["text"] == "void First(void) {}\n"


def test_context_reads_merged_per_pc_archive(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    coverage = tmp_path / "coverage-expanded.json"
    coverage.write_text(json.dumps({"pcs": {
        "0x00000014": {"sample_count": 3, "scenarios": ["first", "second"]},
    }}), encoding="utf-8")

    value = build_context(
        AnalysisDatabase(database_root),
        0x14,
        SymbolStore(symbols=(Symbol(0x10, "FirstFunction", "function"),)),
        coverage_path=coverage,
    )

    assert value["runtime"] == {
        "observed": True,
        "pc_count": 1,
        "scenarios": ["first", "second"],
        "source": str(coverage),
    }


def _write_database(root: Path) -> None:
    root.mkdir(parents=True)
    documents = {
        "metadata.json": {"format": "openaladdin-ghidra-full-rom-v1", "counts": {"functions": 2}},
        "functions.json": [
            {"address": "0x00000010", "name": "First", "start": "0x00000010", "end": "0x00000018"},
            {"address": "0x00000020", "name": "Second", "start": "0x00000020", "end": "0x00000028"},
        ],
        "callgraph.json": {"edges": [{"from": "0x00000010", "to": "0x00000020", "site": "0x00000014"}]},
        "xrefs.json": {"references": [{"from": "0x00000014", "to": "0x00000020", "type": "CALL"}]},
        "memory_reads.json": {"references": [{"from": "0x00000012", "to": "0x00FF0000", "type": "READ"}]},
        "memory_writes.json": {"references": [{"from": "0x00000016", "to": "0x00FF0000", "type": "WRITE"}]},
        "indirect_calls.json": {"references": []},
        "jump_tables.json": {"tables": []},
        "address_classes.json": {"classes": [{"start": "0x00000030", "end": "0x0000003F", "class": "UNKNOWN"}]},
    }
    for filename, value in documents.items():
        (root / filename).write_text(json.dumps(value), encoding="utf-8")


def test_analysis_database_queries_generated_records(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    database = AnalysisDatabase(database_root)

    assert database.function(0x15)["name"] == "First"
    assert database.callers(0x20)[0]["from"] == "0x00000010"
    assert database.callees(0x10)[0]["to"] == "0x00000020"
    assert database.readers(0xFF0000)[0]["from"] == "0x00000012"
    assert database.writers(0xFF0000)[0]["from"] == "0x00000016"
    assert database.unknown()[0]["class"] == "UNKNOWN"


def test_analysis_database_function_references_use_sparse_ranges(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    path = database_root / "functions.json"
    path.write_text(json.dumps([
        {
            "address": "0x00000010",
            "name": "Sparse",
            "start": "0x00000010",
            "end": "0x00000028",
            "ranges": [
                {"start": "0x00000010", "end": "0x00000012"},
                {"start": "0x00000020", "end": "0x00000022"},
            ],
        },
        {"address": "0x00000020", "name": "Second", "start": "0x00000020", "end": "0x00000028"},
    ]), encoding="utf-8")
    xrefs = json.loads((database_root / "xrefs.json").read_text())
    xrefs["references"].extend([
        {"from": "0x00000021", "to": "0x00000030", "type": "DATA"},
        {"from": "0x00000018", "to": "0x00000031", "type": "DATA", "from_function": "0x00000010"},
    ])
    (database_root / "xrefs.json").write_text(json.dumps(xrefs), encoding="utf-8")

    references = AnalysisDatabase(database_root).function_references(0x10, "xrefs.json")
    assert [item["from"] for item in references] == ["0x00000018", "0x00000021"]
