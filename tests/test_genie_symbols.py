from __future__ import annotations

import json
from pathlib import Path

from genie.cli import build_parser
from genie.ghidra.database import AnalysisDatabase
from genie.symbols import SymbolStore, mechanical_name, name_for


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
    assert show.address == 0x1AC784
    assert find.kind == "function"
    assert stats.json_output is True


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
