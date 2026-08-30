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


def test_actor_and_interaction_runtime_bases_have_canonical_aliases():
    symbols = SymbolStore()

    common = symbols.at(0x00FF7E82, include_ranges=False)
    auxiliary = symbols.at(0x00FF84B2, include_ranges=False)
    interaction = symbols.at(0x00FFAE87, include_ranges=False)

    assert common is not None
    assert common.name == "ACTOR_COMMON_POOL_BASE"
    assert common.metadata["alias_of"] == "ACTOR_TABLE_BASE"
    assert common.metadata["entry_offset"] == 0x42
    assert auxiliary is not None
    assert auxiliary.name == "ACTOR_AUXILIARY_POOL_BASE"
    assert auxiliary.metadata["entry_offset"] == 0x672
    assert interaction is not None
    assert interaction.name == "INTERACTION_RUNTIME_TABLE_BASE"
    assert interaction.metadata["entry_offset"] == 3


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


def test_symbol_review_queue_catches_conservative_identity_and_reachability_language(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    queue = symbol_review_queue(
        AnalysisDatabase(database_root),
        SymbolStore(symbols=(
            Symbol(
                0x10,
                "NeutralAsset",
                "data",
                confidence="decompiled",
                description=(
                    "Exact payload; its higher-level scene identity remains intentionally neutral. "
                    "No external consumer or live entry remains unclaimed."
                ),
            ),
            Symbol(0x20, "ClosedData", "data", confidence="confirmed"),
        )),
    )

    assert len(queue) == 1
    assert queue[0]["address"] == "0x00000010"
    assert queue[0]["review_markers"] == [
        "higher-level scene identity remains intentionally neutral",
        "no external consumer",
        "live entry remains unclaimed",
    ]


def test_symbol_review_queue_ignores_closed_range_owners(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    queue = symbol_review_queue(
        AnalysisDatabase(database_root),
        SymbolStore(symbols=(
            Symbol(
                0x10,
                "PhysicalContinuation",
                "function",
                confidence="decompiled",
                description="Physical continuation; live entry remains unclaimed.",
                metadata={"review_status": "closed"},
            ),
            Symbol(
                0x20,
                "OpenQuestion",
                "function",
                confidence="decompiled",
                description="The selector remains unresolved.",
            ),
        )),
    )

    assert [item["address"] for item in queue] == ["0x00000020"]


def test_symbol_review_queue_ignores_closed_data_owners(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    queue = symbol_review_queue(
        AnalysisDatabase(database_root),
        SymbolStore(symbols=(
            Symbol(
                0x10,
                "DuplicatePaletteBand",
                "data",
                confidence="decompiled",
                description="Exact duplicate; no direct consumer remains open.",
                metadata={"review_status": "closed"},
            ),
            Symbol(
                0x20,
                "UnresolvedPaletteBand",
                "data",
                confidence="decompiled",
                description="No direct consumer remains open.",
            ),
        )),
    )

    assert [item["address"] for item in queue] == ["0x00000020"]


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


def test_context_prefers_newest_merged_archive_by_default(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    (tmp_path / "coverage-ghidra.json").write_text(json.dumps({"functions": {
        "0x00000010": {"pc_count": 1, "scenarios": ["old"]},
    }}), encoding="utf-8")
    (tmp_path / "coverage-expanded.json").write_text(json.dumps({"pcs": {
        "0x00000014": {"sample_count": 2, "scenarios": ["new"]},
    }}), encoding="utf-8")

    value = build_context(
        AnalysisDatabase(database_root),
        0x14,
        SymbolStore(symbols=(Symbol(0x10, "FirstFunction", "function"),)),
    )

    assert value["runtime"]["observed"] is True
    assert value["runtime"]["scenarios"] == ["new"]
    assert value["runtime"]["source"].endswith("coverage-expanded.json")


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


def test_real_hud_display_buffers_have_canonical_ram_symbols():
    symbols = SymbolStore()

    active = symbols.at(0x00FF7E29, include_ranges=False)
    assert active is not None
    assert active.name == "HUD_DISPLAY_DIGITS"
    assert active.metadata["type"] == "u8[7]"
    assert active.confidence == "decompiled"

    pending = symbols.at(0x00FF7E30, include_ranges=False)
    assert pending is not None
    assert pending.name == "INTERACTION_PENDING_DISPLAY_VALUE"
    assert pending.metadata["type"] == "u8[7]"
    assert active.address + 7 == pending.address


def test_real_apple_counter_has_a_semantic_projection_alias():
    symbols = SymbolStore()

    counter = symbols.at(0x00FFEFE0, include_ranges=False)
    assert counter is not None
    assert counter.name == "INTERACTION_COUNTER_DIGITS"
    assert "PLAYER_APPLE_COUNT_DIGITS" in counter.aliases
    assert counter.metadata["format"] == "ascii_digits"


def test_real_player_counter_has_a_provisional_life_projection_alias():
    symbols = SymbolStore()

    counter = symbols.at(0x00FF7E3C, include_ranges=False)
    assert counter is not None
    assert counter.name == "GAME_DIFFICULTY_COUNTER"
    assert "PLAYER_LIFE_COUNT_DIGIT" in counter.aliases
    assert counter.metadata["format"] == "ascii_digit"


def test_real_player_interaction_animation_selector_contract():
    symbols = SymbolStore()

    selector = symbols.at(0x00FFF16A, include_ranges=False)
    assert selector is not None
    assert selector.name == "PLAYER_INTERACTION_ANIMATION_INDEX"
    assert selector.metadata["type"] == "u8"

    table = symbols.at(0x001218D8, include_ranges=False)
    assert table is not None
    assert table.name == "PLAYER_INTERACTION_ANIMATION_TABLE"
    assert table.size == 40
    assert table.confidence == "confirmed"


def test_real_player_terrain_response_latches_have_canonical_roles():
    symbols = SymbolStore()

    expected = {
        0x00FFF0D3: "PLAYER_INTERACTION_MARKER",
        0x00FFF0DE: "PLAYER_TERRAIN_PUSH_DOWN_STATE",
        0x00FFF0DF: "PLAYER_TERRAIN_PUSH_UP_STATE",
        0x00FFF0ED: "PLAYER_TERRAIN_RESPONSE_PHASE",
        0x00FFF0EE: "PLAYER_TERRAIN_RESPONSE_TIMER",
        0x00FFF0EF: "PLAYER_TERRAIN_RESPONSE_LEFT",
        0x00FFF0F0: "PLAYER_TERRAIN_RESPONSE_RIGHT",
        0x00FFF101: "PLAYER_TERRAIN_BRAKE_STATE",
    }
    for address, name in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name


def test_real_interaction_anchor_pair_has_canonical_roles():
    symbols = SymbolStore()

    expected = {
        0x00FFF094: "INTERACTION_ANCHOR_X",
        0x00FFF096: "INTERACTION_ANCHOR_Y",
    }
    for address, name in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == "pixels"


def test_real_query_input_bytes_have_canonical_roles():
    symbols = SymbolStore()

    expected = {
        0x00FFF155: "TERRAIN_QUERY_INPUT_RAW",
        0x00FFF156: "TERRAIN_QUERY_FLAGS",
    }
    for address, name in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == "bitfield"


def test_real_level08_event_state_has_canonical_roles():
    symbols = SymbolStore()

    expected = {
        0x00FFF084: "LEVEL08_EVENT_PHASE",
        0x00FFF086: "LEVEL08_EVENT_COUNTER_HIGH",
        0x00FFF088: "LEVEL08_EVENT_COUNTER_LOW",
        0x00FFF08A: "LEVEL08_VDP_RECORD_OFFSET",
        0x00FFF12E: "LEVEL_EVENT_SCRIPT_CURSOR",
    }
    for address, name in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
    assert symbols.at(0x00FFF132, include_ranges=False) is None


def test_real_presentation_actor_slot_has_structural_canonical_roles():
    symbols = SymbolStore()

    actor = symbols.at(0x00FF7EC4, include_ranges=False)
    x = symbols.at(0x00FF7EC6, include_ranges=False)
    y = symbols.at(0x00FF7EC8, include_ranges=False)
    assert actor is not None
    assert actor.name == "ACTOR_TABLE_SLOT_2_BASE"
    assert "OPTION_SELECTION_MARKER_ACTOR" in actor.aliases
    assert x is not None
    assert x.name == "ACTOR_SLOT_2_X"
    assert "OPTION_SELECTION_MARKER_X" in x.aliases
    assert y is not None
    assert y.name == "ACTOR_SLOT_2_Y"
    assert "OPTION_SELECTION_MARKER_Y" in y.aliases


def test_real_palette_render_state_has_canonical_roles():
    symbols = SymbolStore()

    for address, name in {
        0x00FF7262: "PALETTE_BAND_0",
        0x00FF7266: "PALETTE_BAND_1",
        0x00FF726A: "PALETTE_BAND_2",
        0x00FF726E: "PALETTE_BAND_3",
    }.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == "address"

    palette = symbols.at(0x00FF8800, include_ranges=False)
    latch = symbols.at(0x00FF8880, include_ranges=False)
    assert palette is not None
    assert palette.name == "CURRENT_VDP_PALETTE"
    assert palette.size == 0x80
    assert latch is not None
    assert latch.name == "VDP_COMMAND_ADDRESS_LATCH"


def test_real_terrain_work_end_has_canonical_role():
    symbol = SymbolStore().at(0x00FF725C, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "TERRAIN_WORK_END"
    assert symbol.metadata["format"] == "byte_offset"


def test_real_level08_vdp_state_has_canonical_roles():
    symbols = SymbolStore()

    offset = symbols.at(0x00FFF0A2, include_ranges=False)
    order = symbols.at(0x00FFF165, include_ranges=False)
    assert offset is not None
    assert offset.name == "LEVEL08_VDP_SCROLL_OFFSET"
    assert offset.metadata["format"] == "vdp_offset"
    assert order is not None
    assert order.name == "VDP_TILE_PLANE_ORDER"
    assert order.metadata["format"] == "boolean"


def test_real_level07_spawn_cooldown_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF113, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "LEVEL07_SPAWN_COOLDOWN"
    assert symbol.metadata["format"] == "countdown"
    assert symbol.confidence == "decompiled"


def test_real_frame_input_resource_service_gate_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF168, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "FRAME_INPUT_RESOURCE_SERVICE_GATE"
    assert symbol.metadata["format"] == "boolean"
    assert symbol.confidence == "decompiled"


def test_real_scene_resource_rebuild_phase_counter_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF122, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "SCENE_RESOURCE_REBUILD_PHASE_COUNTER"
    assert symbol.metadata["format"] == "countdown"
    assert symbol.confidence == "decompiled"


def test_real_level_timer_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF103, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "LEVEL_TIMER"
    assert symbol.metadata["format"] == "countdown"
    assert symbol.confidence == "decompiled"


def test_real_player_collision_response_gates_have_canonical_roles():
    symbols = SymbolStore()
    type3e = symbols.at(0x00FFF177, include_ranges=False)
    type3f = symbols.at(0x00FFF178, include_ranges=False)
    assert type3e is not None
    assert type3e.name == "PLAYER_COLLISION_GATE_TYPE3E"
    assert type3e.metadata["format"] == "boolean"
    assert type3f is not None
    assert type3f.name == "PLAYER_COLLISION_GATE_TYPE3F"
    assert type3f.metadata["format"] == "boolean"


def test_real_type47_49_collision_gates_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FFF126: "PLAYER_COLLISION_GATE_TYPE47",
        0x00FFF127: "PLAYER_COLLISION_GATE_TYPE48",
        0x00FFF128: "PLAYER_COLLISION_GATE_TYPE49",
    }
    for address, name in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == "boolean"


def test_real_scene_resource_state10_vdp_progress_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF0B6, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "SCENE_RESOURCE_STATE10_VDP_PROGRESS"
    assert symbol.metadata["type"] == "u16"
    assert symbol.metadata["format"] == "integer"


def test_real_scene_resource_tile_base_has_canonical_role():
    symbol = SymbolStore().at(0x00FFEFF0, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "SCENE_RESOURCE_TILE_BASE"
    assert symbol.metadata["type"] == "u16"
    assert symbol.metadata["format"] == "vram_tile_offset"


def test_real_interaction_response_elapsed_ticks_has_canonical_role():
    symbol = SymbolStore().at(0x00FFEFEA, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "ACTOR_RESPONSE_ELAPSED_TICKS"
    assert symbol.metadata["type"] == "u16"
    assert symbol.metadata["format"] == "ticks"


def test_real_player_terrain_transition_gate_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF114, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "PLAYER_TERRAIN_TRANSITION_GATE"
    assert symbol.metadata["type"] == "u8"
    assert symbol.metadata["format"] == "boolean"


def test_real_actor_vm_domain_selector_has_canonical_role():
    symbol = SymbolStore().at(0x00FF7DA2, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "ACTOR_VM_MOVEMENT_PASS"
    assert symbol.metadata["type"] == "u8"
    assert symbol.metadata["format"] == "boolean"


def test_real_actor_vm_continuations_have_canonical_roles():
    symbols = SymbolStore()
    command = symbols.at(0x00FF7D9A, include_ranges=False)
    cursor = symbols.at(0x00FF7D9E, include_ranges=False)
    assert command is not None
    assert command.name == "ACTOR_VM_COMMAND_CONTINUATION"
    assert command.metadata["type"] == "rom_pointer"
    assert cursor is not None
    assert cursor.name == "ACTOR_VM_CURSOR_CLEAR_CONTINUATION"
    assert cursor.metadata["type"] == "rom_pointer"


def test_real_scene_graphics_have_loader_specific_canonical_names():
    symbols = SymbolStore()

    resource_graphics = symbols.at(0x0012D654, include_ranges=False)
    assert resource_graphics is not None
    assert resource_graphics.name == "SCENE_RESOURCE_PALETTE_1298F2_E000_GRAPHICS"
    assert resource_graphics.metadata["type"] == "graphics_data"
    assert resource_graphics.size == 0x21B

    reset_graphics = symbols.at(0x0012E666, include_ranges=False)
    assert reset_graphics is not None
    assert reset_graphics.name == "SCENE_RESET_E000_GRAPHICS"
    assert reset_graphics.metadata["type"] == "graphics_data"
    assert reset_graphics.size == 0x183


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
