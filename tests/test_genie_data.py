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


def test_data_context_decodes_bounded_canonical_stream_without_report(tmp_path):
    database_root = _database(tmp_path)
    rom_path = tmp_path / "rom" / "Disneys_Aladdin_U_p1.bin"
    rom_path.parent.mkdir()
    rom_path.symlink_to(Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin")
    symbols = SymbolStore(symbols=(
        Symbol(
            0x1215D8,
            "ACTOR_MOVE_UNINDEXED_STEP_LOOP_001215D8",
            "data",
            confidence="provisional",
            size=8,
            metadata={"type": "movement_stream"},
        ),
    ))
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=Layout(0x200000, (LayoutRange(0, 0x1FFFFF, "UNKNOWN", "test"),)),
    )

    value = index.context(0x1215D8)
    assert value is not None
    assert value["decoder"]["available"] is True
    assert value["decoder"]["bytes_decoded"] == 8
    assert value["decoder"]["size_matches"] is True
    assert "canonical symbol fallback" in value["decoder"]["source"]


def test_data_context_exposes_canonical_actor_template_stream_pointer(tmp_path):
    database_root = _database(tmp_path)
    rom_path = tmp_path / "rom" / "Disneys_Aladdin_U_p1.bin"
    rom_path.parent.mkdir()
    rom_path.symlink_to(Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin")
    symbols = SymbolStore(symbols=(
        Symbol(
            0x121180,
            "ACTOR_MOVE_TYPE7C_LEVEL_EVENT_PRELUDE",
            "data",
            confidence="provisional",
            size=10,
            metadata={"type": "movement_stream"},
        ),
        Symbol(
            0x1B81B0,
            "ACTOR_TEMPLATE_TYPE_7C_UNINDEXED_LEVEL_EVENT",
            "data",
            confidence="provisional",
            size=20,
            metadata={"type": "actor_template"},
        ),
    ))
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=Layout(0x200000, (LayoutRange(0, 0x1FFFFF, "UNKNOWN", "test"),)),
    )

    value = index.context(0x121180)
    assert value is not None
    assert value["consumers"][0]["name"] == "ACTOR_TEMPLATE_TYPE_7C_UNINDEXED_LEVEL_EVENT"
    assert value["references"][0]["type"] == "ACTOR_TEMPLATE_MOVEMENT_POINTER"


def test_data_context_joins_layout_fragment_to_decoder_root(tmp_path):
    database_root = _database(tmp_path)
    layout = Layout(
        rom_size=0x100,
        ranges=(
            LayoutRange(0x00, 0x3F, "UNKNOWN", "test"),
            LayoutRange(0x40, 0x43, "ANIMATION_STREAM", "tracked.symbol", "AnimationRoot"),
            LayoutRange(0x44, 0x4F, "ANIMATION_STREAM", "animation_streams.json", "ANIM_STREAM_0044"),
            LayoutRange(0x50, 0xFF, "UNKNOWN", "test"),
        ),
    )
    symbols = SymbolStore(symbols=(
        Symbol(0x40, "AnimationRoot", "data", confidence="confirmed", size=4),
    ))
    animation_path = tmp_path / "animation.json"
    animation_path.write_text(json.dumps({
        "streams": {
            "AnimationRoot": {
                "entry": "0x40",
                "bytes_decoded": 16,
                "stopped_reason": "control_flow_cycle",
                "instructions": [{"address": "0x40", "size": 16}],
            },
        },
    }), encoding="utf-8")
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=layout,
        animation_path=animation_path,
    )

    value = index.context(0x44)
    assert value is not None
    assert value["decoder"]["available"] is True
    assert value["decoder"]["covered_by_root"] is True
    assert value["decoder"]["root_entry"] == "0x00000040"
    assert value["decoder"]["size_matches"] is True


def test_data_index_collapses_interior_layout_fragment_owned_by_canonical_symbol(tmp_path):
    database_root = _database(tmp_path)
    layout = Layout(
        rom_size=0x100,
        ranges=(
            LayoutRange(0x00, 0x3F, "UNKNOWN", "test"),
            LayoutRange(0x40, 0x43, "POINTER_TABLE", "tracked.symbol", "DispatchTable"),
            LayoutRange(0x44, 0x47, "POINTER_TABLE", "tracked.symbol", "DispatchTable"),
            LayoutRange(0x48, 0x4F, "POINTER_TABLE", "tracked.symbol", "DispatchTable"),
            LayoutRange(0x50, 0xFF, "UNKNOWN", "test"),
        ),
    )
    symbols = SymbolStore(symbols=(
        Symbol(
            0x40,
            "DispatchTable",
            "data",
            confidence="confirmed",
            size=0x10,
            metadata={"type": "rom_pointer_table", "entry_size": 4, "count": 4},
        ),
        Symbol(
            0x44,
            "DispatchTableEntry01",
            "data",
            confidence="confirmed",
            metadata={"type": "rom_pointer"},
        ),
    ))
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=layout,
    )

    objects = index.objects(kind="pointer-table")
    assert [(item["start"], item["end"]) for item in objects] == [("0x00000040", "0x0000004F")]
    assert index.at(0x44)["name"] == "DispatchTableEntry01"


def test_data_index_collapses_generic_fragment_inside_canonical_stream(tmp_path):
    database_root = _database(tmp_path)
    layout = Layout(
        rom_size=0x100,
        ranges=(
            LayoutRange(0x00, 0x3F, "UNKNOWN", "test"),
            LayoutRange(0x40, 0x43, "MOVEMENT_STREAM", "tracked.symbol", "MovementRoot"),
            LayoutRange(0x44, 0x47, "OPAQUE_DATA", "ghidra.defined_data"),
            LayoutRange(0x48, 0x4F, "UNKNOWN", "test"),
            LayoutRange(0x50, 0xFF, "UNKNOWN", "test"),
        ),
    )
    symbols = SymbolStore(symbols=(
        Symbol(
            0x40,
            "MovementRoot",
            "data",
            confidence="decompiled",
            size=0x10,
            metadata={"type": "movement_stream"},
        ),
    ))
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=layout,
    )

    objects = index.objects(kind="all")
    assert [(item["start"], item["end"]) for item in objects if item["start"] == "0x00000040"] == [
        ("0x00000040", "0x0000004F"),
    ]
    assert index.at(0x44)["name"] == "MovementRoot"


def test_data_index_collapses_generic_fragment_inside_validated_manifest_range(tmp_path):
    database_root = _database(tmp_path)
    layout = Layout(
        rom_size=0x100,
        ranges=(
            LayoutRange(0x00, 0x3F, "UNKNOWN", "test"),
            LayoutRange(0x40, 0x4F, "GRAPHICS", "sprites.frame_manifest", "SPRITE_FRAME_0000"),
            LayoutRange(0x44, 0x47, "OPAQUE_DATA", "ghidra.defined_data"),
            LayoutRange(0x50, 0xFF, "UNKNOWN", "test"),
        ),
    )
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=SymbolStore(symbols=()),
        layout=layout,
    )

    objects = index.objects(kind="all")
    assert [(item["start"], item["end"], item["name"]) for item in objects] == [
        ("0x00000040", "0x0000004F", "SPRITE_FRAME_0000"),
    ]
    assert index.at(0x44)["name"] == "SPRITE_FRAME_0000"
    assert index.todo(kind="all", unresolved_only=True) == []


def test_data_index_collapses_palette_bank_layout_fragment(tmp_path):
    database_root = _database(tmp_path)
    layout = Layout(
        rom_size=0x100,
        ranges=(
            LayoutRange(0x00, 0x3F, "UNKNOWN", "test"),
            LayoutRange(0x40, 0x43, "GRAPHICS", "tracked.symbol", "PaletteSource"),
            LayoutRange(0x44, 0x47, "GRAPHICS", "tracked.symbol", "PaletteSource"),
            LayoutRange(0x48, 0x4F, "GRAPHICS", "tracked.symbol", "PaletteSource"),
            LayoutRange(0x50, 0xFF, "UNKNOWN", "test"),
        ),
    )
    symbols = SymbolStore(symbols=(
        Symbol(
            0x40,
            "PaletteSource",
            "data",
            confidence="confirmed",
            size=0x10,
            metadata={"type": "palette_data"},
        ),
    ))
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=layout,
    )

    objects = index.objects(kind="graphics")
    assert [(item["start"], item["end"]) for item in objects] == [("0x00000040", "0x0000004F")]
    assert index.context(0x44)["object"]["name"] == "PaletteSource"


def test_data_index_trusts_validated_manifest_extent_confidence(tmp_path):
    database_root = _database(tmp_path)
    layout = Layout(
        rom_size=0x100,
        ranges=(
            LayoutRange(0x00, 0x3F, "UNKNOWN", "test"),
            LayoutRange(0x40, 0x4F, "GRAPHICS", "sprites.frame_manifest", "SPRITE_FRAME_0000"),
            LayoutRange(0x50, 0x5F, "COMPRESSED_DATA", "assets.manifest", "RNC_BLOCK_000050"),
            LayoutRange(0x60, 0xFF, "UNKNOWN", "test"),
        ),
    )
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=SymbolStore(symbols=()),
        layout=layout,
    )

    frame = index.at(0x40)
    block = index.at(0x50)
    assert frame is not None and frame["confidence"] == "confirmed"
    assert block is not None and block["confidence"] == "confirmed"
    assert index.todo(kind="all", unresolved_only=True) == []


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


def test_data_todo_can_limit_queue_to_rom_objects(tmp_path):
    database_root = _database(tmp_path)
    symbols = SymbolStore(symbols=(
        Symbol(0x10, "RomObject", "data", size=4),
        Symbol(0xFF0000, "RamObject", "data"),
    ))
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=Layout(0x100, (LayoutRange(0, 0xFF, "UNKNOWN", "test"),)),
    )

    all_items = index.todo(kind="all")
    rom_items = index.todo(kind="all", rom_only=True)
    assert {item["name"] for item in all_items} == {"RomObject", "RamObject"}
    assert [item["name"] for item in rom_items] == ["RomObject"]


def test_data_todo_can_limit_queue_to_unresolved_objects(tmp_path):
    database_root = _database(tmp_path)
    symbols = SymbolStore(symbols=(
        Symbol(0x10, "ConfirmedObject", "data", confidence="confirmed", size=4),
        Symbol(0x20, "ProvisionalObject", "data", confidence="provisional", size=4),
        Symbol(0x30, "UnknownObject", "data", confidence="unknown", size=4),
    ))
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=Layout(0x100, (LayoutRange(0, 0xFF, "UNKNOWN", "test"),)),
    )

    items = index.todo(kind="all", unresolved_only=True)
    assert [item["name"] for item in items] == ["ProvisionalObject", "UnknownObject"]


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
    todo = build_parser().parse_args([
        "data", "todo", "--kind", "animation", "--limit", "4", "--rom-only", "--unresolved-only",
    ])
    next_item = build_parser().parse_args([
        "data", "next", "--kind", "actor-template", "--rom-only", "--unresolved-only",
    ])
    context = build_parser().parse_args(["data", "context", "0x00121964", "--json"])
    assert stats.json_output is True
    assert todo.kind == "animation"
    assert todo.limit == 4
    assert todo.rom_only is True
    assert todo.unresolved_only is True
    assert next_item.kind == "actor-template"
    assert next_item.rom_only is True
    assert next_item.unresolved_only is True
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


def test_data_index_accepts_injected_semantic_provider(tmp_path):
    database_root = _database(tmp_path)
    seen: dict[str, object] = {}

    class Provider:
        def classify_symbol(self, symbol):
            return "custom-data" if symbol.name == "CustomObject" else None

        def decoded_references(self, obj):
            seen["decoded"] = obj["decoded"]
            value = obj["value"]
            if value["kind"] != "custom-data":
                return ()
            return ({
                "from": "0x00000020",
                "from_function_name": "CustomConsumer",
                "to": value["start"],
                "type": "CUSTOM_EVIDENCE",
            },)

    symbols = SymbolStore(symbols=(Symbol(0x40, "CustomObject", "data", size=4),))
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=Layout(0x100, (LayoutRange(0, 0xFF, "UNKNOWN", "test"),)),
        providers=(Provider(),),
    )

    context = index.context(0x40)
    assert context is not None
    assert context["object"]["kind"] == "custom-data"
    assert context["references"][0]["type"] == "CUSTOM_EVIDENCE"
    assert isinstance(seen["decoded"], dict)


def test_data_index_without_providers_keeps_core_fallback_generic(tmp_path):
    database_root = _database(tmp_path)
    symbols = SymbolStore(symbols=(Symbol(0x40, "PlayerAnimation", "data", size=4),))
    index = DataIndex(
        AnalysisDatabase(database_root),
        root=tmp_path,
        symbols=symbols,
        layout=Layout(0x100, (LayoutRange(0, 0xFF, "UNKNOWN", "test"),)),
        providers=(),
    )

    value = index.at(0x40)
    assert value is not None
    assert value["kind"] == "rom-data"
