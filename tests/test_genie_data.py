from __future__ import annotations

import json
from pathlib import Path

from genie.cli import build_parser
from genie.data import DataIndex
from genie.ghidra.database import AnalysisDatabase
from genie.layout.model import Layout, LayoutRange
from genie.symbols import Symbol, SymbolStore


def _database(root: Path) -> Path:
    database = root / "full-rom"
    database.mkdir(parents=True)
    documents = {
        "metadata.json": {"format": "test", "rom_size": 0x100},
        "xrefs.json": {"references": [
            {
                "from": "0x20",
                "from_function": "0x20",
                "from_function_name": "TerrainHandler_SnapToGridBlock",
                "to": "0x40",
                "type": "DATA",
            },
        ]},
    }
    for filename in (
        "functions.json", "instructions.json", "callgraph.json", "memory_reads.json",
        "memory_writes.json", "indirect_calls.json", "jump_tables.json", "address_classes.json",
    ):
        documents[filename] = [] if filename == "functions.json" else {"references": []}
    documents["jump_tables.json"] = {"tables": []}
    documents["address_classes.json"] = {"classes": []}
    for filename, value in documents.items():
        (database / filename).write_text(json.dumps(value), encoding="utf-8")
    return database


def test_data_context_joins_layout_decoder_and_consumer(tmp_path):
    database_root = _database(tmp_path)
    layout = Layout(
        rom_size=0x100,
        ranges=(
            LayoutRange(0x00, 0x3F, "UNKNOWN", "test"),
            LayoutRange(0x40, 0x4F, "ANIMATION_STREAM", "tracked.symbol", "TerrainAnimation"),
            LayoutRange(0x50, 0xFF, "UNKNOWN", "test"),
        ),
    )
    symbols = SymbolStore(symbols=(
        Symbol(0x40, "TerrainAnimation", "data", confidence="confirmed", size=0x10),
        Symbol(0x20, "TerrainHandler_SnapToGridBlock", "function"),
    ))
    (tmp_path / "animation.json").write_text(json.dumps({
        "streams": {
            "TerrainAnimation": {
                "entry": "0x40",
                "bytes_decoded": 16,
                "stopped_reason": "unconditional_jump",
                "instructions": [{"address": "0x40", "size": 16}],
            },
        },
    }), encoding="utf-8")
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=layout,
        animation_path=tmp_path / "animation.json",
    )

    value = index.context(0x44)
    assert value is not None
    assert value["object"]["name"] == "TerrainAnimation"
    assert value["object"]["size"] == 16
    assert value["decoded"] is True
    assert value["decoder"]["size_matches"] is True
    assert value["consumers"][0]["name"] == "TerrainHandler_SnapToGridBlock"
    assert value["overlap"] == []


def test_data_todo_filters_aliases_and_prioritizes_missing_decode(tmp_path):
    database_root = _database(tmp_path)
    symbols = SymbolStore(symbols=(
        Symbol(0x10, "AnimationRoot", "data", size=4),
        Symbol(0x20, "ActorTemplate", "data", metadata={"type": "actor_template"}),
    ))
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=Layout(0x100, (LayoutRange(0, 0xFF, "UNKNOWN", "test"),)),
    )
    items = index.todo(kind="animation_stream")
    assert len(items) == 1
    assert items[0]["name"] == "AnimationRoot"
    assert "not_decoded" in items[0]["reasons"]


def test_data_todo_ignores_embedded_stream_alias(tmp_path):
    database_root = _database(tmp_path)
    symbols = SymbolStore(symbols=(
        Symbol(
            0x10,
            "BrakeEntry",
            "data",
            metadata={"alias_of": "TransitionPresentation", "entry_offset": 8},
        ),
    ))
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=Layout(
            0x100,
            (
                LayoutRange(0, 0x0F, "UNKNOWN", "test"),
                LayoutRange(0x10, 0x10, "ANIMATION_STREAM", "tracked.symbol", "BrakeEntry"),
                LayoutRange(0x11, 0xFF, "UNKNOWN", "test"),
            ),
        ),
    )

    assert index.todo(kind="animation_stream") == []
    context = index.context(0x10)
    assert context is not None
    assert context["decoder"]["aliased"] is True
    assert context["decoder"]["alias_of"] == "TransitionPresentation"


def test_data_cli_surface_dispatches():
    stats = build_parser().parse_args(["data", "stats", "--json"])
    todo = build_parser().parse_args(["data", "todo", "--kind", "animation", "--limit", "4"])
    next_item = build_parser().parse_args(["data", "next", "--kind", "actor-template"])
    context = build_parser().parse_args(["data", "context", "0x00121964", "--json"])
    assert stats.json_output is True
    assert todo.kind == "animation"
    assert todo.limit == 4
    assert next_item.kind == "actor-template"
    assert context.address == 0x121964


def test_data_infers_bounded_pointer_and_table_extents(tmp_path):
    database_root = _database(tmp_path)
    symbols = SymbolStore(symbols=(
        Symbol(0x10, "Pointer", "data", metadata={"type": "rom_pointer"}),
        Symbol(
            0x20,
            "PointerTable",
            "data",
            metadata={"type": "rom_pointer_table", "entry_size": 4, "count": 3},
        ),
    ))
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=Layout(0x100, (LayoutRange(0, 0xFF, "UNKNOWN", "test"),)),
    )

    pointer = index.at(0x10)
    table = index.at(0x20)
    assert pointer is not None and pointer["range_bounded"] is True
    assert pointer["size"] == 4
    assert table is not None and table["range_bounded"] is True
    assert table["end"] == "0x0000002B"


def test_data_context_includes_animation_f5_template_consumer(tmp_path):
    database_root = _database(tmp_path)
    symbols = SymbolStore(symbols=(
        Symbol(0x40, "PlayerAnimation", "data", size=16),
        Symbol(0x80, "ActorTemplate", "data", metadata={"type": "actor_template"}),
    ))
    (tmp_path / "animation.json").write_text(json.dumps({
        "streams": {
            "PlayerAnimation": {
                "entry": "0x40",
                "name": "PlayerAnimation",
                "instructions": [{
                    "address": "0x44",
                    "opcode": "0xF5",
                    "raw": "F5000000008000000000000000000000",
                }],
            },
        },
    }), encoding="utf-8")
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=Layout(0x100, (LayoutRange(0, 0xFF, "UNKNOWN", "test"),)),
        animation_path=tmp_path / "animation.json",
    )

    context = index.context(0x80)
    assert context is not None
    assert context["consumers"][0]["name"] == "PlayerAnimation"
    assert context["references"][0]["type"] == "ANIMATION_F5_TEMPLATE"
