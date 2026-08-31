from __future__ import annotations

import json
from pathlib import Path

from genie.cli import build_parser
from genie.ghidra.context import build_context
from genie.ghidra.database import AnalysisDatabase
from genie.ghidra.worklist import function_work_queue, symbol_review_queue, unresolved_symbol_queue
from genie.layout.model import Layout, LayoutRange
from genie.symbols import (
    Symbol,
    SymbolStore,
    edit_symbol,
    is_low_information_name,
    mechanical_name,
    name_for,
)
from genie.symbols.entities import SemanticMapping, load_entity_mappings, validate_entity_mappings
from genie.symbols.type_worklist import (
    candidate_class,
    numeric_type_ids,
    numeric_type_inventory,
    numeric_type_work_queue,
)


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


def test_symbol_naming_identifies_type_oriented_collision_names():
    assert is_low_information_name("ActorType04_ActorCollisionHandler") is True
    assert is_low_information_name("ActorType02_08_09_ActorCollisionHandler") is True
    assert is_low_information_name("ActorType1E_PrepareRecoveryPlane") is True
    assert is_low_information_name("InteractionSpawn_RuntimeType20") is True
    assert is_low_information_name("InteractionSpawn_RuntimeType47_B1") is True
    assert is_low_information_name("InteractionGate_RuntimeType40_AD") is True
    assert is_low_information_name("InteractionSpawn_Type84Base_B6") is False
    assert is_low_information_name("Actor_ApplyTerminalCollisionResponse") is False


def test_symbols_cli_surface_dispatches():
    show = build_parser().parse_args(["symbols", "show", "0x001AC784"])
    find = build_parser().parse_args(["symbols", "find", "AnimationVM", "--kind", "function"])
    stats = build_parser().parse_args(["symbols", "stats", "--json"])
    unknown = build_parser().parse_args(["symbols", "unknown", "--kind", "function", "--limit", "4"])
    semantic = build_parser().parse_args(["symbols", "next", "--kind", "function", "--semantic"])
    review = build_parser().parse_args(["symbols", "review", "--kind", "data", "--limit", "4", "--json"])
    unresolved = build_parser().parse_args(["symbols", "unresolved", "--kind", "ram", "--limit", "4", "--json"])
    rename = build_parser().parse_args(["symbols", "rename", "0x20", "Scene_Init"])
    describe = build_parser().parse_args(["symbols", "describe", "0x20", "entry point"])
    confidence = build_parser().parse_args(["symbols", "confidence", "0x20", "decompiled"])
    type_worklist = build_parser().parse_args(["symbols", "type-worklist", "--kind", "data", "--limit", "4", "--json"])
    assert show.address == 0x1AC784
    assert find.kind == "function"
    assert stats.json_output is True
    assert unknown.limit == 4
    assert semantic.semantic is True
    assert review.kind == "data"
    assert review.limit == 4
    assert review.json_output is True
    assert unresolved.kind == "ram"
    assert unresolved.limit == 4
    assert unresolved.json_output is True
    assert rename.name == "Scene_Init"
    assert describe.description == "entry point"
    assert confidence.confidence == "decompiled"
    assert type_worklist.kind == "data"
    assert type_worklist.limit == 4
    assert type_worklist.json_output is True


def test_numeric_type_inventory_extracts_technical_selectors_without_renaming():
    assert numeric_type_ids("ACTOR_ANIM_TYPE2D_GUARD_SWORD_ATTACK") == (0x2D,)
    assert numeric_type_ids("InteractionSpawn_RuntimeType17_2D_2E_2F_4E_4F") == (0x17, 0x2D, 0x2E, 0x2F, 0x4E, 0x4F)
    assert numeric_type_ids("ACTOR_TEMPLATE_TYPE_7F_TYPE36_CHILD") == (0x7F, 0x36)
    assert numeric_type_ids("InteractionSpawn_Type84Base_B6") == (0x84, 0xB6)
    assert candidate_class("ACTOR_ANIM_TYPE2D_GUARD_SWORD_ATTACK") == "entity"
    assert candidate_class("ACTOR_MOVE_TYPE75_LEVEL_EXIT") == "event"
    assert candidate_class("ACTOR_TYPE42_COLLISION_STEP") == "event"
    assert candidate_class("ACTOR_TEMPLATE_TYPE01") == "technical_only"

    symbols = SymbolStore(symbols=(
        Symbol(0x10, "ACTOR_ANIM_TYPE2D_GUARD_SWORD_ATTACK", "data"),
        Symbol(0x20, "ACTOR_TEMPLATE_TYPE01", "data"),
        Symbol(0x30, "KnownName", "data"),
    ))
    inventory = numeric_type_inventory(symbols.symbols)
    assert inventory["numeric_canonical_names"] == 2
    assert inventory["by_kind"] == {"data": 2}
    assert inventory["technical_only"] == 1


def test_semantic_mapping_model_preserves_type_and_symbol_identity():
    mapping = SemanticMapping.from_dict({
        "name": "GUARD",
        "scope": "actor",
        "symbol_addresses": ["0x10"],
        "technical_types": ["0x2D"],
        "confidence": "trace_validated",
        "evidence": ["guard-trace-v1"],
    })
    assert mapping.to_dict()["symbol_addresses"] == ["0x00000010"]
    assert mapping.to_dict()["technical_types"] == ["0x2D"]
    assert validate_entity_mappings([mapping]) == []
    assert validate_entity_mappings([SemanticMapping(name="NoEvidence", scope="actor")]) == [
        "semantic mapping has no technical identity: NoEvidence",
        "semantic mapping has no evidence: NoEvidence",
    ]


def test_numeric_type_work_queue_uses_reference_and_runtime_evidence(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    (tmp_path / "coverage-ghidra.json").write_text(json.dumps({"functions": {
        "0x00000010": {"pc_count": 3, "scenarios": ["game"]},
    }}), encoding="utf-8")
    (database_root / "memory_reads.json").write_text(json.dumps({"references": [
        {"from": "0x00000012", "from_function": "0x00000010", "to": "0x00000040", "type": "READ"},
    ]}), encoding="utf-8")
    symbols = SymbolStore(symbols=(
        Symbol(0x40, "ACTOR_ANIM_TYPE2D_GUARD_SWORD_ATTACK", "data", size=4),
        Symbol(0x50, "ACTOR_TEMPLATE_TYPE01", "data", size=4),
    ))
    queue = numeric_type_work_queue(
        AnalysisDatabase(database_root),
        symbols,
        coverage_path=tmp_path / "coverage-ghidra.json",
    )
    assert [item["name"] for item in queue] == [
        "ACTOR_ANIM_TYPE2D_GUARD_SWORD_ATTACK",
        "ACTOR_TEMPLATE_TYPE01",
    ]
    assert queue[0]["technical_type_ids"] == ["0x2D"]
    assert queue[0]["runtime_observed"] is True
    assert queue[0]["candidate_class"] == "entity"
    assert queue[1]["mapping_status"] == "technical_only"

    mapped = numeric_type_work_queue(
        AnalysisDatabase(database_root),
        symbols,
        mappings=(SemanticMapping(
            name="GUARD",
            scope="actor",
            symbol_addresses=(0x40,),
            technical_types=(0x2D,),
            confidence="trace_validated",
            evidence=("guard-trace-v1",),
        ),),
    )
    assert mapped[0]["candidate_class"] == "mapped"
    assert mapped[0]["mapping_status"] == "mapped"
    assert mapped[0]["mapping_matches"] == [{"match": "symbol", "name": "GUARD", "scope": "actor"}]


def test_guard_and_handhold_numeric_identities_promote_to_semantic_names_with_aliases():
    symbols = SymbolStore()
    expected = {
        0x00123490: ("ACTOR_ANIM_GUARD_SPAWN", "ACTOR_ANIM_TYPE1D_GUARD_SPAWN"),
        0x00123EE8: ("ACTOR_ANIM_GUARD_SWORD_ATTACK", "ACTOR_ANIM_TYPE2D_GUARD_SWORD_ATTACK"),
        0x00124034: ("ACTOR_ANIM_HANDHOLD_PROMOTION_RESPONSE", "ACTOR_ANIM_TYPE6C_INTERACTION_RESPONSE"),
        0x00124046: ("ACTOR_ANIM_HANDHOLD_INTERACTION", "ACTOR_ANIM_TYPE69_HANDHOLD"),
        0x001B7D78: ("ACTOR_TEMPLATE_HANDHOLD_INTERACTION", "ACTOR_TEMPLATE_TYPE_69_INTERACTION"),
        0x001B7D8C: ("ACTOR_TEMPLATE_HANDHOLD_REMOTE", "ACTOR_TEMPLATE_TYPE_6A_6B"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = load_entity_mappings()
    assert validate_entity_mappings(mappings) == []
    mapping_names = {mapping.name for mapping in mappings}
    assert {"GUARD_SWORD_ATTACK", "HANDHOLD_INTERACTION"} <= mapping_names
    assert "LEVEL11_EVENT" in mapping_names


def test_level_event_movement_names_promote_roles_and_preserve_numeric_aliases():
    symbols = SymbolStore()
    expected = {
        0x00120352: ("ACTOR_MOVE_TERMINAL_DEATH", "ACTOR_MOVE_TYPE84_DEATH_TERMINAL"),
        0x001203D0: ("ACTOR_MOVE_LEVEL12_TERMINAL_EVENT", "ACTOR_MOVE_TYPE2F_LEVEL12_TERMINAL_EVENT"),
        0x001203F2: ("ACTOR_MOVE_LEVEL09_SPAWN", "ACTOR_MOVE_TYPE50_LEVEL09_SPAWN"),
        0x001209C6: ("ACTOR_MOVE_SCENE_TABLE_TRANSITION", "ACTOR_MOVE_TYPE84_SCENE_TABLE_TRANSITION"),
        0x00120A42: ("ACTOR_MOVE_LEVEL_EXIT", "ACTOR_MOVE_TYPE75_LEVEL_EXIT"),
        0x0012120E: ("ACTOR_MOVE_LEVEL11_EVENT", "ACTOR_MOVE_TYPE7B_LEVEL11_EVENT"),
        0x00121226: ("ACTOR_MOVE_LEVEL11_EVENT_DISTANCE_ENTRY", "ACTOR_MOVE_TYPE7B_LEVEL11_EVENT_DISTANCE_ENTRY"),
        0x0012123E: ("ACTOR_MOVE_LEVEL11_EVENT_COMPARE_TRANSITION", "ACTOR_MOVE_TYPE7B_LEVEL11_EVENT_COMPARE_TRANSITION"),
        0x0012152A: ("ACTOR_MOVE_LEVEL08_EXIT_PRESENTATION", "ACTOR_MOVE_TYPE60_LEVEL08_EXIT_PRESENTATION"),
        0x001217A2: ("ACTOR_MOVE_SCENE_RESET", "ACTOR_MOVE_TYPE84_SCENE_RESET"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases


def test_shared_interaction_names_promote_roles_and_preserve_numeric_aliases():
    symbols = SymbolStore()
    expected = {
        0x001217B4: ("ACTOR_MOVE_WALL_OSCILLATION", "ACTOR_MOVE_TYPE34_WALL_OSCILLATION"),
        0x00122BD8: ("ACTOR_ANIM_INTERACTION_RESPONSE_SHARED", "ACTOR_ANIM_TYPE3A_3B_INTERACTION_RESPONSE"),
        0x00122C12: ("ACTOR_ANIM_INTERACTION_SHARED", "ACTOR_ANIM_TYPE40_INTERACTION"),
        0x00123E36: ("ACTOR_ANIM_INTERACTION_CHILD_SPAWN", "ACTOR_ANIM_TYPE84_RUNTIME47_4C"),
        0x00123FE4: ("ACTOR_ANIM_INTERACTION_TERMINAL_RESPONSE", "ACTOR_ANIM_TYPE2B_INTERACTION"),
        0x0012585C: ("ACTOR_ANIM_LEVEL_ENTRY_SHARED", "ACTOR_ANIM_TYPE84_LEVEL_ENTRY_SHARED"),
        0x001B7904: ("ACTOR_TEMPLATE_INTERACTION_SHARED_BASE", "ACTOR_TEMPLATE_TYPE_6E_73_BASE"),
        0x001B7A1C: ("ACTOR_TEMPLATE_INTERACTION_POSITION_VARIANTS", "ACTOR_TEMPLATE_TYPE_55_INTERACTION_7B_7E"),
        0x001B8138: ("ACTOR_TEMPLATE_LEVEL_ENTRY_SHARED", "ACTOR_TEMPLATE_TYPE_84_LEVEL_ENTRY_SHARED"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = load_entity_mappings()
    assert validate_entity_mappings(mappings) == []
    mapping_names = {mapping.name for mapping in mappings}
    assert {
        "WALL_OSCILLATION",
        "INTERACTION_RESPONSE_SHARED",
        "INTERACTION_SHARED",
        "INTERACTION_CHILD_SPAWN",
        "INTERACTION_TERMINAL_RESPONSE",
        "LEVEL_ENTRY_SHARED",
        "INTERACTION_SHARED_BASE",
        "INTERACTION_POSITION_VARIANTS",
    } <= mapping_names
    mappings_by_name = {mapping.name: mapping for mapping in mappings}
    assert all(
        mappings_by_name[name].scope == "role"
        for name in (
            "WALL_OSCILLATION",
            "INTERACTION_RESPONSE_SHARED",
            "INTERACTION_SHARED",
            "INTERACTION_CHILD_SPAWN",
            "INTERACTION_TERMINAL_RESPONSE",
            "INTERACTION_SHARED_BASE",
            "INTERACTION_POSITION_VARIANTS",
        )
    )


def test_interaction_pair_movement_names_promote_roles_and_preserve_aliases():
    symbols = SymbolStore()
    expected = {
        0x0011F8A4: ("ACTOR_MOVE_INTERACTION_PAIR_ANCHOR_APPROACH", "ACTOR_MOVE_TYPE5E84_PAIR_E1E2"),
        0x0011FAA8: ("ACTOR_MOVE_INTERACTION_PAIR_ANCHOR_RESPONSE", "ACTOR_MOVE_TYPE5E84_PAIR_ANCHOR_RESPONSE"),
        0x0011FD18: ("ACTOR_MOVE_INTERACTION_PAIR_TRAJECTORY", "ACTOR_MOVE_TYPE5E84_PAIR_E3E5"),
        0x0012004E: ("ACTOR_MOVE_INTERACTION_PAIR_SHORT_RESPONSE", "ACTOR_MOVE_TYPE5E84_PAIR_E6"),
        0x001200DE: ("ACTOR_MOVE_INTERACTION_PAIR_READY_RESPONSE", "ACTOR_MOVE_TYPE5E84_PAIR_F9"),
        0x001201FE: ("ACTOR_MOVE_INTERACTION_PAIR_EXTENDED_RESPONSE", "ACTOR_MOVE_TYPE5E84_PAIR_E8"),
        0x001202CA: ("ACTOR_MOVE_INTERACTION_PAIR_VERTICAL_RESPONSE", "ACTOR_MOVE_TYPE5E84_PAIR_E9"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    pair_mapping = next(mapping for mapping in load_entity_mappings() if mapping.name == "INTERACTION_PAIR")
    assert pair_mapping.scope == "role"
    assert set(expected) <= set(pair_mapping.symbol_addresses)


def test_player_collision_names_promote_roles_and_preserve_aliases():
    symbols = SymbolStore()
    expected = {
        0x00001EEE: ("ACTOR_COLLISION_HANDLER_HORIZONTAL_FACING_TOGGLE", "ACTOR_COLLISION_HANDLER_TYPE_0D"),
        0x00121618: ("ACTOR_MOVE_PLAYER_COLLISION_CHILD_SPAWN", "ACTOR_MOVE_TYPE3E_3F_PLAYER_COLLISION_RESPONSE"),
        0x00122DB2: ("ACTOR_ANIM_PLAYER_COLLISION_RECOVERY", "ACTOR_ANIM_TYPE84_TYPE01_RESPONSE"),
        0x00122E16: ("ACTOR_ANIM_PLAYER_COLLISION_CHILD_SPAWN", "ACTOR_ANIM_TYPE84_TYPE03_COLLISION_RESPONSE"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = {mapping.name: mapping for mapping in load_entity_mappings()}
    assert validate_entity_mappings(mappings.values()) == []
    assert mappings["HORIZONTAL_FACING_TOGGLE"].scope == "role"
    assert mappings["PLAYER_COLLISION_RECOVERY"].scope == "role"
    assert mappings["PLAYER_COLLISION_CHILD_SPAWN"].scope == "role"


def test_random_spawn_variant_names_promote_role_and_preserve_aliases():
    symbols = SymbolStore()
    expected = {
        0x001241F8: ("ACTOR_ANIM_RANDOM_SPAWN_VARIANT", "ACTOR_ANIM_TYPE89_RANDOM_VARIANT"),
        0x001B7DA0: ("ACTOR_TEMPLATE_RANDOM_SPAWN_VARIANT", "ACTOR_TEMPLATE_TYPE_89_RANDOM_VARIANT"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mapping = next(mapping for mapping in load_entity_mappings() if mapping.name == "RANDOM_SPAWN_VARIANT")
    assert validate_entity_mappings(load_entity_mappings()) == []
    assert mapping.scope == "role"
    assert set(expected) == set(mapping.symbol_addresses)


def test_terminal_transition_and_level_object_names_promote_roles_and_preserve_aliases():
    symbols = SymbolStore()
    expected = {
        0x00121CB0: ("ACTOR_ANIM_TERMINAL_TRANSITION_PRIMARY", "ACTOR_ANIM_TYPE84_TERMINAL_TRANSITION_PRIMARY"),
        0x00121CCE: ("ACTOR_ANIM_TERMINAL_TRANSITION_SECONDARY", "ACTOR_ANIM_TYPE84_TERMINAL_TRANSITION_SECONDARY"),
        0x001B7A08: ("ACTOR_TEMPLATE_LEVEL_OBJECT_BASE", "ACTOR_TEMPLATE_TYPE_5A_LEVEL_OBJECT"),
        0x001B7B48: ("ACTOR_TEMPLATE_TRANSITION_PRESENTATION", "ACTOR_TEMPLATE_TRANSITION_TYPE_84"),
        0x001B7878: ("ACTOR_TEMPLATE_TERMINAL_TRANSITION_SECONDARY", "ACTOR_TEMPLATE_TYPE_84_TERMINAL_TRANSITION_SECONDARY"),
        0x001B788C: ("ACTOR_TEMPLATE_TERMINAL_TRANSITION_PRIMARY", "ACTOR_TEMPLATE_TYPE_84_TERMINAL_TRANSITION_PRIMARY"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = load_entity_mappings()
    assert validate_entity_mappings(mappings) == []
    mapping_names = {mapping.name for mapping in mappings}
    assert {"TERMINAL_TRANSITION", "TRANSITION_PRESENTATION", "LEVEL_OBJECT_BASE"} <= mapping_names
    mappings_by_name = {mapping.name: mapping for mapping in mappings}
    assert mappings_by_name["TERMINAL_TRANSITION"].scope == "event"
    assert mappings_by_name["TRANSITION_PRESENTATION"].scope == "role"
    assert mappings_by_name["LEVEL_OBJECT_BASE"].scope == "role"


def test_interaction_child_and_vertical_object_names_promote_roles_and_preserve_aliases():
    symbols = SymbolStore()
    expected = {
        0x00120B62: ("ACTOR_MOVE_INTERACTION_CHILD_SHARED", "ACTOR_MOVE_TYPE31_F5_CHILD_SHARED"),
        0x00120F76: ("ACTOR_MOVE_VERTICAL_BOB", "ACTOR_MOVE_TYPE2A_VERTICAL_BOB"),
        0x00123154: ("ACTOR_ANIM_INTERACTION_PAIR_COMPANION", "ACTOR_ANIM_TYPE84_INTERACTION_PAIR_COMPANION"),
        0x001231DC: ("ACTOR_ANIM_VERTICAL_BOB", "ACTOR_ANIM_TYPE2A_VERTICAL_BOB"),
        0x00124450: ("ACTOR_ANIM_INTERACTION_SPAWN_RESPONSE", "ACTOR_ANIM_TYPE43_8A_INTERACTION"),
        0x001B7AF8: ("ACTOR_TEMPLATE_VERTICAL_BOB_OBJECT", "ACTOR_TEMPLATE_TYPE_2A"),
        0x001B79E0: ("ACTOR_TEMPLATE_INTERACTION_SPAWN_BASE", "ACTOR_TEMPLATE_TYPE_43"),
        0x001B8098: ("ACTOR_TEMPLATE_INTERACTION_RESPONSE_CHILD", "ACTOR_TEMPLATE_F5_TYPE_7F"),
        0x001B8034: ("ACTOR_TEMPLATE_INTERACTION_CHILD_A", "ACTOR_TEMPLATE_TYPE_31_F5_CHILD_A"),
        0x001B8048: ("ACTOR_TEMPLATE_INTERACTION_CHILD_B", "ACTOR_TEMPLATE_TYPE_31_F5_CHILD_B"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = load_entity_mappings()
    assert validate_entity_mappings(mappings) == []
    mapping_names = {mapping.name for mapping in mappings}
    assert {
        "INTERACTION_PAIR_COMPANION",
        "VERTICAL_BOB_OBJECT",
        "INTERACTION_SPAWN_RESPONSE",
        "INTERACTION_CHILD_MOVEMENT",
    } <= mapping_names
    mappings_by_name = {mapping.name: mapping for mapping in mappings}
    assert all(
        mappings_by_name[name].scope == "role"
        for name in (
            "INTERACTION_PAIR_COMPANION",
            "VERTICAL_BOB_OBJECT",
            "INTERACTION_SPAWN_RESPONSE",
            "INTERACTION_CHILD_MOVEMENT",
        )
    )


def test_exit_and_transition_families_promote_semantic_names_and_preserve_aliases():
    symbols = SymbolStore()
    expected = {
        0x00121D5A: ("ACTOR_ANIM_TRANSITION_INTERACTION_SHARED", "ACTOR_ANIM_TYPE29_TRANSITION_SHARED"),
        0x0012434C: ("ACTOR_ANIM_LEVEL_EXIT_PRESENTATION", "ACTOR_ANIM_EXIT_TYPE84_PRESENTATION"),
        0x001244E6: ("ACTOR_ANIM_TERRAIN_EXIT_RESPONSE", "ACTOR_ANIM_TYPE74_TERRAIN_EXIT_RESPONSE"),
        0x001B7B70: ("ACTOR_TEMPLATE_TRANSITION_INTERACTION", "ACTOR_TEMPLATE_TYPE_29_INTERACTION"),
        0x001B7E04: ("ACTOR_TEMPLATE_LEVEL_EXIT_PRESENTATION", "ACTOR_TEMPLATE_EXIT_TYPE_84"),
        0x001B7E68: ("ACTOR_TEMPLATE_TERRAIN_EXIT_RESPONSE", "ACTOR_TEMPLATE_TYPE_74_TERRAIN_RESPONSE"),
        0x001B7E7C: ("ACTOR_TEMPLATE_LEVEL_EXIT_CHILD", "ACTOR_TEMPLATE_TYPE_84_LEVEL_EXIT_CHILD"),
        0x001B82F0: ("ACTOR_TEMPLATE_LEVEL06_EXIT", "ACTOR_TEMPLATE_TYPE_74_LEVEL06_EXIT"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = {mapping.name: mapping for mapping in load_entity_mappings()}
    assert validate_entity_mappings(mappings.values()) == []
    assert {
        "LEVEL_EXIT_PRESENTATION",
        "TRANSITION_INTERACTION_SHARED",
        "TERRAIN_EXIT_RESPONSE",
        "LEVEL_EXIT_CHILD",
        "LEVEL06_EXIT",
    } <= mappings.keys()


def test_interaction_response_family_promotes_semantic_roles_and_preserves_aliases():
    symbols = SymbolStore()
    expected = {
        0x00124658: ("ACTOR_ANIM_INTERACTION_GUARD_RESPONSE", "ACTOR_ANIM_TYPE13_INTERACTION"),
        0x00124766: ("ACTOR_ANIM_INTERACTION_WALL_RESPONSE", "ACTOR_ANIM_TYPE14_INTERACTION"),
        0x001248EA: ("ACTOR_ANIM_PRESENTATION_CHILD", "ACTOR_ANIM_TYPE84_PRESENTATION_CHILD"),
        0x001B7F1C: ("ACTOR_TEMPLATE_INTERACTION_GUARD_RESPONSE", "ACTOR_TEMPLATE_TYPE_13_INTERACTION"),
        0x001B7F30: ("ACTOR_TEMPLATE_INTERACTION_WALL_RESPONSE", "ACTOR_TEMPLATE_TYPE_14_INTERACTION"),
        0x001B7F44: ("ACTOR_TEMPLATE_PRESENTATION_CHILD", "ACTOR_TEMPLATE_TYPE_84_PRESENTATION_CHILD"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = {mapping.name: mapping for mapping in load_entity_mappings()}
    assert validate_entity_mappings(mappings.values()) == []
    assert {"INTERACTION_GUARD_RESPONSE", "INTERACTION_WALL_RESPONSE", "PRESENTATION_CHILD"} <= mappings.keys()


def test_interaction_spawn_and_presentation_response_names_preserve_aliases():
    symbols = SymbolStore()
    expected = {
        0x00121710: ("ACTOR_MOVE_INTERACTION_PRESENTATION_CHILD", "ACTOR_MOVE_TYPE4D_TYPE12_RESPONSE_CHILD"),
        0x001239CA: ("ACTOR_ANIM_INTERACTION_RESPONSE_SPAWN", "ACTOR_ANIM_TYPE10_INTERACTION_RESPONSE"),
        0x0012512C: ("ACTOR_ANIM_INTERACTION_PRESENTATION_RESPONSE", "ACTOR_ANIM_TYPE12_INTERACTION_RESPONSE"),
        0x00126074: ("ACTOR_ANIM_INTERACTION_PRESENTATION_CHILD", "ACTOR_ANIM_TYPE4D_INTERACTION_RESPONSE_CHILD"),
        0x001B7B84: ("ACTOR_TEMPLATE_INTERACTION_RESPONSE_SPAWN", "ACTOR_TEMPLATE_TYPE_10_INTERACTION_RESPONSE"),
        0x001B7EA4: ("ACTOR_TEMPLATE_INTERACTION_PRESENTATION_RESPONSE", "ACTOR_TEMPLATE_TYPE_12_INTERACTION_RESPONSE"),
        0x001B83B8: ("ACTOR_TEMPLATE_INTERACTION_PRESENTATION_CHILD", "ACTOR_TEMPLATE_TYPE_4D_TYPE12_RESPONSE_CHILD"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = {mapping.name: mapping for mapping in load_entity_mappings()}
    assert validate_entity_mappings(mappings.values()) == []
    assert {"INTERACTION_RESPONSE_SPAWN", "INTERACTION_PRESENTATION_RESPONSE"} <= mappings.keys()


def test_upper_and_shared_presentation_names_preserve_numeric_aliases():
    symbols = SymbolStore()
    expected = {
        0x00123358: ("ACTOR_ANIM_UPPER_INTERACTION", "ACTOR_ANIM_UPPER_TYPE20"),
        0x001233CC: ("ACTOR_ANIM_UPPER_COLLISION_RESPONSE", "ACTOR_ANIM_TYPE20_COLLISION_RESPONSE"),
        0x00124CE4: ("ACTOR_ANIM_PRESENTATION_SHARED", "ACTOR_ANIM_TYPE1A_PRESENTATION"),
        0x001B7C10: ("ACTOR_TEMPLATE_UPPER_INTERACTION", "ACTOR_TEMPLATE_UPPER_TYPE20"),
        0x001B7C24: ("ACTOR_TEMPLATE_UPPER_PROXIMITY_INTERACTION", "ACTOR_TEMPLATE_UPPER_TYPE1E"),
        0x001B7FF8: ("ACTOR_TEMPLATE_PRESENTATION_SHARED", "ACTOR_TEMPLATE_TYPE_1A_PRESENTATION_BASE"),
        0x001B800C: ("ACTOR_TEMPLATE_PRESENTATION_CHILD_INITIAL", "ACTOR_TEMPLATE_TYPE_84_TYPE_1A_PRESENTATION_CHILD"),
        0x001B82DC: ("ACTOR_TEMPLATE_PRESENTATION_VARIANT", "ACTOR_TEMPLATE_TYPE_84_TYPE_1A_PRESENTATION_VARIANT"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = {mapping.name: mapping for mapping in load_entity_mappings()}
    assert validate_entity_mappings(mappings.values()) == []
    assert {"UPPER_INTERACTION", "PRESENTATION_SHARED"} <= mappings.keys()


def test_interaction_state_response_names_preserve_numeric_aliases():
    symbols = SymbolStore()
    expected = {
        0x00125A68: ("ACTOR_ANIM_INTERACTION_STATE_ONE", "ACTOR_ANIM_TYPE7A_INTERACTION_06"),
        0x00125A88: ("ACTOR_ANIM_INTERACTION_STATE_FIFTY_FIVE", "ACTOR_ANIM_TYPE7A_INTERACTION_07"),
        0x00125AA8: ("ACTOR_ANIM_INTERACTION_STATE_AB", "ACTOR_ANIM_TYPE7A_INTERACTION_08"),
        0x00125AC8: ("ACTOR_ANIM_INTERACTION_STATE_RESPONSE", "ACTOR_ANIM_TYPE7A_INTERACTION_RESPONSE"),
        0x001B8264: ("ACTOR_TEMPLATE_INTERACTION_STATE_RESPONSE", "ACTOR_TEMPLATE_TYPE_7A_INTERACTION"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = {mapping.name: mapping for mapping in load_entity_mappings()}
    assert validate_entity_mappings(mappings.values()) == []
    assert mappings["INTERACTION_STATE_RESPONSE"].scope == "role"


def test_compact_interaction_response_names_preserve_numeric_aliases():
    symbols = SymbolStore()
    expected = {
        0x00122C40: ("ACTOR_ANIM_INTERACTION_RESPONSE_COMPACT", "ACTOR_ANIM_TYPE44_INTERACTION"),
        0x00122C66: ("ACTOR_ANIM_INTERACTION_MULTI_CHILD_SPAWN", "ACTOR_ANIM_TYPE46_SHARED_SPAWN"),
        0x001B79CC: ("ACTOR_TEMPLATE_INTERACTION_MULTI_CHILD_SPAWN", "ACTOR_TEMPLATE_TYPE_46_SHARED_SPAWN"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = {mapping.name: mapping for mapping in load_entity_mappings()}
    assert validate_entity_mappings(mappings.values()) == []
    assert {"INTERACTION_COMPACT_RESPONSE", "INTERACTION_MULTI_CHILD_SPAWN"} <= mappings.keys()


def test_level12_scene_and_terminal_terrain_names_preserve_aliases():
    symbols = SymbolStore()
    expected = {
        0x00125AFE: ("ACTOR_ANIM_LEVEL12_SCENE_EVENT", "ACTOR_ANIM_TYPE59_SCENE_EVENT"),
        0x00125C26: ("ACTOR_ANIM_TERMINAL_TERRAIN_TRANSITION", "ACTOR_ANIM_TYPE84_TERMINAL_TERRAIN"),
        0x001B8278: ("ACTOR_TEMPLATE_LEVEL12_SCENE_EVENT", "ACTOR_TEMPLATE_TYPE_59_SCENE_EVENT"),
        0x001B82A0: ("ACTOR_TEMPLATE_TERMINAL_TERRAIN_TRANSITION", "ACTOR_TEMPLATE_TYPE_84_TERMINAL_TERRAIN"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = {mapping.name: mapping for mapping in load_entity_mappings()}
    assert validate_entity_mappings(mappings.values()) == []
    assert {"LEVEL12_SCENE_EVENT", "TERMINAL_TERRAIN_TRANSITION"} <= mappings.keys()


def test_terminal_interaction_family_names_preserve_numeric_aliases():
    symbols = SymbolStore()
    expected = {
        0x00125CC6: ("ACTOR_ANIM_TERMINAL_INTERACTION", "ACTOR_ANIM_TYPE84_TERMINAL_INTERACTION"),
        0x00125D12: ("ACTOR_ANIM_TERMINAL_INTERACTION_CHILD", "ACTOR_ANIM_TYPE84_TERMINAL_INTERACTION_CHILD"),
        0x00126030: ("ACTOR_ANIM_TERMINAL_INTERACTION_RESPONSE", "ACTOR_ANIM_TYPE84_TERMINAL_INTERACTION_RESPONSE"),
        0x001B82B4: ("ACTOR_TEMPLATE_TERMINAL_INTERACTION", "ACTOR_TEMPLATE_TYPE_84_TERMINAL_INTERACTION"),
        0x001B82C8: ("ACTOR_TEMPLATE_TERMINAL_INTERACTION_CHILD", "ACTOR_TEMPLATE_TYPE_84_TERMINAL_INTERACTION_CHILD"),
        0x001B8458: ("ACTOR_TEMPLATE_TERMINAL_INTERACTION_RESPONSE", "ACTOR_TEMPLATE_TYPE_84_TERMINAL_INTERACTION_RESPONSE"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = {mapping.name: mapping for mapping in load_entity_mappings()}
    assert validate_entity_mappings(mappings.values()) == []
    assert mappings["TERMINAL_INTERACTION"].scope == "event"


def test_level04_event_response_names_preserve_numeric_aliases():
    symbols = SymbolStore()
    expected = {
        0x00125348: ("ACTOR_ANIM_LEVEL04_EVENT_RESPONSE_ALTERNATE", "ACTOR_ANIM_TYPE58_INTERACTION_ALTERNATE"),
        0x00125392: ("ACTOR_ANIM_LEVEL04_EVENT_RESPONSE", "ACTOR_ANIM_TYPE58_INTERACTION"),
        0x001B8084: ("ACTOR_TEMPLATE_LEVEL04_EVENT_RESPONSE", "ACTOR_TEMPLATE_TYPE_58_INTERACTION"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = {mapping.name: mapping for mapping in load_entity_mappings()}
    assert validate_entity_mappings(mappings.values()) == []
    assert mappings["LEVEL04_EVENT_RESPONSE"].scope == "event"


def test_interaction_proximity_response_names_preserve_numeric_aliases():
    symbols = SymbolStore()
    expected = {
        0x00125DEA: ("ACTOR_ANIM_INTERACTION_PROXIMITY_RESPONSE", "ACTOR_ANIM_TYPE84_INTERACTION_FD_FE_ROOT"),
        0x00125E08: ("ACTOR_ANIM_INTERACTION_PROXIMITY_RESPONSE_VARIANT_A", "ACTOR_ANIM_TYPE84_INTERACTION_FD_FE_VARIANT_A"),
        0x00125E40: ("ACTOR_ANIM_INTERACTION_PROXIMITY_RESPONSE_VARIANT_B", "ACTOR_ANIM_TYPE84_INTERACTION_FD_FE_VARIANT_B"),
        0x001B8354: ("ACTOR_TEMPLATE_INTERACTION_PROXIMITY_RESPONSE", "ACTOR_TEMPLATE_TYPE_84_INTERACTION_FD_FE"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = {mapping.name: mapping for mapping in load_entity_mappings()}
    assert validate_entity_mappings(mappings.values()) == []
    assert mappings["INTERACTION_PROXIMITY_RESPONSE"].scope == "role"


def test_random_collision_and_frame_response_names_preserve_aliases():
    symbols = SymbolStore()
    expected = {
        0x00123CF8: ("ACTOR_ANIM_INTERACTION_RANDOM_RESPONSE", "ACTOR_ANIM_TYPE0F_INTERACTION"),
        0x00123D34: ("ACTOR_ANIM_INTERACTION_RANDOM_CHILD", "ACTOR_ANIM_TYPE84_TYPE0F_CHILD"),
        0x00124B16: ("ACTOR_ANIM_INTERACTION_FRAME_RESPONSE", "ACTOR_ANIM_TYPE4E_INTERACTION"),
        0x001295F2: ("INTERACTION_COLLISION_RESPONSE_PALETTE", "INTERACTION_TYPE11_PALETTE_SOURCE"),
        0x001B7C74: ("ACTOR_TEMPLATE_INTERACTION_RANDOM_RESPONSE", "ACTOR_TEMPLATE_TYPE_0F_INTERACTION"),
        0x001B7C88: ("ACTOR_TEMPLATE_INTERACTION_RANDOM_CHILD", "ACTOR_TEMPLATE_TYPE_84_TYPE0F_CHILD"),
        0x001B7E90: ("ACTOR_TEMPLATE_INTERACTION_COLLISION_RESPONSE", "ACTOR_TEMPLATE_TYPE_11_COLLISION_RESPONSE"),
        0x001B7F80: ("ACTOR_TEMPLATE_INTERACTION_FRAME_RESPONSE", "ACTOR_TEMPLATE_TYPE_4E_INTERACTION"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    mappings = {mapping.name: mapping for mapping in load_entity_mappings()}
    assert validate_entity_mappings(mappings.values()) == []
    assert {"INTERACTION_RANDOM_RESPONSE", "INTERACTION_COLLISION_RESPONSE_PALETTE", "INTERACTION_FRAME_RESPONSE"} <= mappings.keys()


def test_actor_template_roles_promote_numeric_names_with_aliases():
    symbols = SymbolStore()
    expected = {
        0x001B7CC4: ("ACTOR_TEMPLATE_UPPER_COLLISION_RESPONSE", "ACTOR_TEMPLATE_COLLISION_TYPE_84"),
        0x001B8368: ("ACTOR_TEMPLATE_COLLISION_RESPONSE_CHILD", "ACTOR_TEMPLATE_TYPE_3D_COLLISION_RESPONSE_CHILD"),
        0x001B7CD8: ("ACTOR_TEMPLATE_WALL_RESPONSE", "ACTOR_TEMPLATE_TYPE_8D_WALL_RESPONSE"),
        0x001B7DF0: ("ACTOR_TEMPLATE_TERRAIN_RESPONSE_ALTERNATE", "ACTOR_TEMPLATE_TYPE_84_TERRAIN_RESPONSE_ALT"),
        0x001B78A0: ("ACTOR_TEMPLATE_INTERACTION_PAIR_BASE", "ACTOR_TEMPLATE_TYPE_5E_INTERACTION_PAIR_BASE"),
        0x001B78C8: ("ACTOR_TEMPLATE_INTERACTION_PAIR_COMPANION", "ACTOR_TEMPLATE_TYPE_84_INTERACTION_PAIR_COMPANION"),
        0x001B78F0: ("ACTOR_TEMPLATE_INTERACTION_BASE", "ACTOR_TEMPLATE_TYPE_84_INTERACTION_BASE"),
        0x001B7F6C: ("ACTOR_TEMPLATE_PRESENTATION_RESPONSE", "ACTOR_TEMPLATE_TYPE_8B_PRESENTATION_RESPONSE"),
        0x001B7A44: ("ACTOR_TEMPLATE_MENU_PRESENTATION", "ACTOR_TEMPLATE_MENU_PRESENTATION_TYPE_84"),
        0x001B8318: ("ACTOR_TEMPLATE_SCENE_INITIAL_PRIMARY", "ACTOR_TEMPLATE_TYPE_84_SCENE_INITIAL_PRIMARY"),
        0x001B832C: ("ACTOR_TEMPLATE_SCENE_INITIAL_SECONDARY", "ACTOR_TEMPLATE_TYPE_84_SCENE_INITIAL_SECONDARY"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases


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


def test_function_work_queue_can_include_weak_semantic_names(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    database = AnalysisDatabase(database_root)
    symbols = SymbolStore(symbols=(
        Symbol(0x10, "ActorType04_ActorCollisionHandler", "function", confidence="confirmed"),
        Symbol(0x20, "SecondFunction", "function", confidence="confirmed"),
    ))

    assert function_work_queue(database, symbols) == []
    queue = function_work_queue(database, symbols, include_weak_names=True)
    assert len(queue) == 1
    assert queue[0]["candidate_reason"] == "weak semantic name"


def test_unresolved_symbol_queue_ranks_runtime_writers_and_excludes_closed_aliases(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    (tmp_path / "coverage-ghidra.json").write_text(
        json.dumps({"functions": {
            "0x00000010": {"pc_count": 4, "scenarios": ["game"]},
        }}),
        encoding="utf-8",
    )
    database = AnalysisDatabase(database_root)
    symbols = SymbolStore(symbols=(
        Symbol(
            0xFF0010,
            "RuntimeRamQuestion",
            "ram",
            confidence="decompiled",
            description="No direct static reader is currently exported.",
        ),
        Symbol(
            0xFF0020,
            "StaticRamQuestion",
            "ram",
            confidence="decompiled",
            description="The producer remains unresolved.",
        ),
        Symbol(
            0x30,
            "ClosedAliasOwner",
            "data",
            confidence="decompiled",
            size=4,
            description="No exported wrapper remains.",
            metadata={"review_status": "closed"},
        ),
        Symbol(
            0x34,
            "InteriorAlias",
            "data",
            confidence="decompiled",
            metadata={"alias_of": "ClosedAliasOwner"},
            description="No exported wrapper remains.",
        ),
    ))
    # Replace the fixture's single RAM reference with evidence for both
    # candidates, including a runtime-observed writer for the first one.
    (database_root / "memory_reads.json").write_text(json.dumps({"references": [
        {"from": "0x00000012", "from_function": "0x00000010", "to": "0x00FF0010", "type": "READ"},
        {"from": "0x00000022", "from_function": "0x00000020", "to": "0x00FF0020", "type": "READ"},
    ]}), encoding="utf-8")
    (database_root / "memory_writes.json").write_text(json.dumps({"references": [
        {"from": "0x00000016", "from_function": "0x00000010", "to": "0x00FF0010", "type": "WRITE"},
        {"from": "0x00000026", "from_function": "0x00000020", "to": "0x00FF0020", "type": "WRITE"},
    ]}), encoding="utf-8")

    queue = unresolved_symbol_queue(
        database,
        symbols,
        coverage_path=tmp_path / "coverage-ghidra.json",
    )

    assert [item["name"] for item in queue] == ["RuntimeRamQuestion", "StaticRamQuestion"]
    assert queue[0]["writer_count"] == 1
    assert queue[0]["reader_count"] == 1
    assert queue[0]["runtime_observed"] is True
    assert queue[0]["writers"][0]["name"] == "First"
    assert all(item["name"] not in {"ClosedAliasOwner", "InteriorAlias"} for item in queue)


def test_unresolved_symbol_queue_filters_by_kind(tmp_path):
    database_root = tmp_path / "full-rom"
    _write_database(database_root)
    symbols = SymbolStore(symbols=(
        Symbol(0xFF0010, "RamQuestion", "ram", description="The producer remains unresolved."),
        Symbol(0x30, "DataQuestion", "data", description="The producer remains unresolved."),
    ))

    queue = unresolved_symbol_queue(AnalysisDatabase(database_root), symbols, kind="data")

    assert [item["name"] for item in queue] == ["DataQuestion"]


def test_real_proximity_collision_handler_has_semantic_name_and_legacy_alias():
    symbol = SymbolStore().at(0x001AE796, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "PlayerCollision_HandleProximityResponse"
    assert "ActorType1E_PlayerCollisionHandler" in symbol.aliases
    assert symbol.confidence == "confirmed"


def test_real_collision_trampolines_have_behavior_names_and_legacy_aliases():
    symbols = SymbolStore()
    expected = {
        0x001AE9D4: ("PlayerCollision_ProcessInteractionState", "ActorType7B_PlayerCollisionHandler", "trace_validated"),
        0x001AE9DA: ("PlayerCollision_DelegateToSharedResponse", "ActorType06_0F_PlayerCollisionHandler", "confirmed"),
    }
    for address, (name, alias, confidence) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == confidence


def test_shared_player_collision_service_has_semantic_name_and_legacy_alias():
    symbols = SymbolStore()

    delegate = symbols.at(0x001AE9DA, include_ranges=False)
    assert delegate is not None
    assert delegate.name == "PlayerCollision_DelegateToSharedResponse"
    assert "PlayerCollision_DelegateToActorBlock" in delegate.aliases
    assert "ActorType06_0F_PlayerCollisionHandler" in delegate.aliases

    service = symbols.at(0x001AEC00, include_ranges=False)
    assert service is not None
    assert service.name == "Actor_ProcessSharedPlayerCollision"
    assert service.aliases == ("Actor_HandlePlayerCollisionBlock",)
    assert service.confidence == "confirmed"


def test_real_actor_terminal_interaction_has_semantic_name_and_legacy_alias():
    symbol = SymbolStore().at(0x001AC458, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "ActorCollision_HandleTerminalInteraction"
    assert "ActorType0A_ActorCollisionHandler" in symbol.aliases
    assert symbol.confidence == "confirmed"


def test_type40_interaction_spawn_helper_has_semantic_name_and_legacy_alias():
    symbol = SymbolStore().at(0x001B7332, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "InteractionSpawn_CreateType40Variant"
    assert "InteractionSpawn_RuntimeType40" in symbol.aliases
    assert symbol.confidence == "decompiled"


def test_type6e_73_interaction_spawn_handlers_have_semantic_names_and_legacy_aliases():
    expected = {
        0x001ACE90: ("InteractionSpawn_CreateType6EActor", "InteractionSpawn_RuntimeType6E"),
        0x001ACECC: ("InteractionSpawn_CreateType6FActor", "InteractionSpawn_RuntimeType6F"),
        0x001ACF08: ("InteractionSpawn_CreateType70Actor", "InteractionSpawn_RuntimeType70"),
        0x001ACF44: ("InteractionSpawn_CreateType71Actor", "InteractionSpawn_RuntimeType71"),
        0x001ACF80: ("InteractionSpawn_CreateType72Actor", "InteractionSpawn_RuntimeType72"),
        0x001ACFBC: ("InteractionSpawn_CreateType73Actor", "InteractionSpawn_RuntimeType73"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_type3c_3e_3f_interaction_response_spawners_have_semantic_names_and_aliases():
    expected = {
        0x001B738A: ("InteractionSpawn_CreateType3CResponse", "InteractionSpawn_RuntimeType3C_42"),
        0x001B73C2: ("InteractionSpawn_CreateType3EResponse", "InteractionSpawn_RuntimeType3E_5F"),
        0x001B73F2: ("InteractionSpawn_CreateType3FResponse", "InteractionSpawn_RuntimeType3F_41"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_type17_palette_interaction_spawn_has_semantic_name_and_legacy_alias():
    symbol = SymbolStore().at(0x001B75D6, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "InteractionSpawn_CreateType17WithPalette"
    assert "InteractionSpawn_RuntimeType17_2D_2E_2F_4E_4F" in symbol.aliases
    assert symbol.confidence == "decompiled"


def test_late_interaction_response_handlers_have_semantic_names_and_aliases():
    expected = {
        0x001B6F0C: ("InteractionSpawn_CreateType87Response", "InteractionSpawn_RuntimeType87_80"),
        0x001B71E0: ("InteractionSpawn_CreateType3BResponse", "InteractionSpawn_RuntimeType3B_66"),
        0x001B7258: ("InteractionGate_CreateType3AResponse", "InteractionGate_RuntimeType3A_AC"),
        0x001B728E: ("InteractionSpawn_CreateType41Response", "InteractionSpawn_RuntimeType41_C8"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases


def test_type47_4c_interaction_spawn_handlers_have_semantic_names_and_aliases():
    expected = {
        0x001B7018: ("InteractionSpawn_CreateType47Actor", "InteractionSpawn_RuntimeType47_B1"),
        0x001B703C: ("InteractionSpawn_CreateType48Actor", "InteractionSpawn_RuntimeType48_B2"),
        0x001B7060: ("InteractionSpawn_CreateType49Actor", "InteractionSpawn_RuntimeType49_B3"),
        0x001B7084: ("InteractionSpawn_CreateType4AActor", "InteractionSpawn_RuntimeType4A_B4"),
        0x001B70B0: ("InteractionSpawn_CreateType4BActor", "InteractionSpawn_RuntimeType4B_B5"),
        0x001B70D4: ("InteractionSpawn_CreateType4CActor", "InteractionSpawn_RuntimeType4C_B6"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_type20_21_22_interaction_spawn_handlers_have_semantic_names_and_aliases():
    expected = {
        0x001B6EB2: ("InteractionSpawn_CreateType20UpperActor", "InteractionSpawn_RuntimeType20"),
        0x001B6ED0: ("InteractionSpawn_CreateType21ProximityActor", "InteractionSpawn_RuntimeType21_1A"),
        0x001B6EEE: ("InteractionSpawn_CreateType22ProximityActor", "InteractionSpawn_RuntimeType22_19"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence in {"confirmed", "decompiled"}

    type21 = symbols.at(0x001B6ED0, include_ranges=False)
    assert type21 is not None
    assert "InteractionHandler_Type21" in type21.aliases


def test_type52_interaction_spawn_handlers_have_semantic_names_and_aliases():
    expected = {
        0x001B750C: ("InteractionSpawn_CreateType52DifficultyVariant_5B", "InteractionSpawn_RuntimeType52_5B"),
        0x001B752E: ("InteractionSpawn_CreateType52DifficultyVariant_5D", "InteractionSpawn_RuntimeType52_5D"),
        0x001B7550: ("InteractionSpawn_CreateType52PresentationVariant_CB", "InteractionSpawn_RuntimeType52_CB"),
        0x001B7566: ("InteractionSpawn_CreateType52PresentationVariant_CC", "InteractionSpawn_RuntimeType52_CC"),
        0x001B757C: ("InteractionSpawn_CreateType52PresentationVariant_CD", "InteractionSpawn_RuntimeType52_CD"),
        0x001B7592: ("InteractionSpawn_CreateType52PresentationVariant_CE", "InteractionSpawn_RuntimeType52_CE"),
        0x001B75A8: ("InteractionSpawn_CreateType52PresentationVariant_CF", "InteractionSpawn_RuntimeType52_CF"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_type37_43_8a_84_34_interaction_spawn_handlers_have_semantic_names_and_aliases():
    expected = {
        0x001B720C: ("InteractionSpawn_CreateType37ResponseActor", "InteractionSpawn_RuntimeType37_6B"),
        0x001B7232: ("InteractionSpawn_CreateType43Actor", "InteractionSpawn_RuntimeType43_40"),
        0x001B723E: ("InteractionSpawn_CreateType8AFromType43Template", "InteractionSpawn_RuntimeType8A_FF"),
        0x001B72AE: ("InteractionSpawn_CreateType84InteractionActor", "InteractionSpawn_RuntimeType84_C9"),
        0x001B72D4: ("InteractionSpawn_CreateType34WallActor", "InteractionSpawn_RuntimeType34_53"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_type40_interaction_spawn_family_has_semantic_names_and_aliases():
    expected = {
        0x001B72FC: ("InteractionSpawn_CreateType40FourPositionSet", "InteractionSpawn_RuntimeType40_FourPosition_61"),
        0x001B7354: ("InteractionGate_CreateType40FromSelectorAD", "InteractionGate_RuntimeType40_AD"),
        0x001B735E: ("InteractionSpawn_CreateType40InteractionActor", "InteractionSpawn_RuntimeType40_60"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_late_compact_interaction_spawn_wrappers_have_semantic_names_and_aliases():
    expected = {
        0x001B744A: ("InteractionGate_CreateType44FromSelectorAB", "InteractionSpawn_RuntimeType44_AB"),
        0x001B7454: ("InteractionSpawn_CreateType44Actor", "InteractionSpawn_RuntimeType44_50"),
        0x001B7494: ("InteractionSpawn_CreateType5AActor", "InteractionSpawn_RuntimeType5A_8E"),
        0x001B74B2: ("InteractionSpawn_CreateType5CFromType5ATemplate", "InteractionSpawn_RuntimeType5C_90"),
        0x001B74C4: ("InteractionSpawn_CreateType5DFromType5ATemplate", "InteractionSpawn_RuntimeType5D_91"),
        0x001B74D6: ("InteractionSpawn_CreateType01Actor", "InteractionSpawn_RuntimeType01_87"),
        0x001B74E2: ("InteractionSpawn_CreateType4EWithFacingOverride", "InteractionSpawn_RuntimeType4E_BB"),
        0x001B74F4: ("InteractionSpawn_CreateType4EActor", "InteractionSpawn_RuntimeType4E_A9"),
        0x001B7500: ("InteractionSpawn_CreateType4FActor", "InteractionSpawn_RuntimeType4F_A6"),
        0x001B75BE: ("InteractionSpawn_CreateType53WithPositionOffset", "InteractionSpawn_RuntimeType53_17"),
        0x001B75F6: ("InteractionSpawn_CreateType84LevelEventActor", "InteractionSpawn_RuntimeType84_30"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_unindexed_interaction_spawn_variants_have_semantic_names_and_aliases():
    expected = {
        0x001B6BFC: ("InteractionSpawn_CreateType57FromType55TemplateVariant", "InteractionSpawn_RuntimeType57_AdjacentVariant"),
        0x001B7474: ("InteractionSpawn_CreateType45ActorVariant", "InteractionSpawn_RuntimeType45_AdjacentVariant"),
        0x001B74A0: ("InteractionSpawn_CreateType5BFromType5ATemplateVariant", "InteractionSpawn_RuntimeType5B_AdjacentVariant"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"
        assert symbol.metadata["review_status"] == "closed"


def test_remaining_static_review_closure_preserves_bounded_limitations():
    expected = {
        0x0011F800,
        0x00123DE2,
        0x001B7990,
        0x001B7A58,
        0x00FFF0F6,
    }
    symbols = SymbolStore()
    for address in expected:
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.metadata["review_status"] == "closed"

    finding = json.loads(
        Path("re/mame/findings/20260831-semantic-review-closure-static-v1.json")
        .read_text(encoding="utf-8")
    )
    assert finding["status"] == "recorded-static-review-closure"
    assert len(finding["closed_symbols"]) == 8
    assert any("not that every indirect selector" in item for item in finding["limitations"])
    assert "TERRAIN_RESPONSE_AUXILIARY_FLAG" in finding["conclusion"]


def test_terrain_auxiliary_flag_is_closed_as_unconsumed_static_state():
    symbol = SymbolStore().at(0x00FFF0E4, include_ranges=False)
    assert symbol is not None
    assert symbol.metadata["review_status"] == "closed"

    finding = json.loads(
        Path("re/mame/findings/20260831-terrain-response-auxiliary-flag-closure-v1.json")
        .read_text(encoding="utf-8")
    )
    assert finding["closure"]["writer_count"] == 1
    assert finding["closure"]["direct_reader_count"] == 0
    assert finding["closure"]["runtime_observed"] is False
    assert any("indirect pointer-based" in item for item in finding["limitations"])


def test_level_table_callbacks_have_phase_accurate_names_and_legacy_aliases():
    expected = {
        0x001B5B66: ("Level00_FrameCallback", "Level00_EnterRoutine"),
        0x001B63EA: ("Level00_ExitCallback", "Level00_ExitRoutine"),
        0x001B5B4A: ("Level01_FrameCallback", "Level01_EnterRoutine"),
        0x001B6406: ("Level01_ExitCallback", "Level01_ExitRoutine"),
        0x001B5B94: ("Level02_FrameCallback", "Level02_EnterRoutine"),
        0x001B6394: ("Level02_ExitCallback", "Level02_ExitRoutine"),
        0x001B5B9A: ("Level03_FrameCallback", "Level03_EnterRoutine"),
        0x001B6414: ("Level03_ExitCallback", "Level03_ExitRoutine"),
        0x001B5B9C: ("Level04_FrameCallback", "Level04_EnterRoutine"),
        0x001B642E: ("Level04_ExitCallback", "Level04_ExitRoutine"),
        0x001B5C20: ("Level05_FrameCallback", "Level05_EnterRoutine"),
        0x001B6434: ("Level05_ExitCallback", "Level05_ExitRoutine"),
        0x001B5D3A: ("Level06_FrameCallback", "Level06_EnterRoutine"),
        0x001B644E: ("Level06_ExitCallback", "Level06_ExitRoutine"),
        0x001B5D68: ("Level07_FrameCallback", "Level07_EnterRoutine"),
        0x001B64C2: ("Level07_ExitCallback", "Level07_ExitRoutine"),
        0x001B6066: ("Level08_FrameCallback", "Level08_EnterRoutine"),
        0x001B64D0: ("Level08_ExitCallback", "Level08_ExitRoutine"),
        0x001B614C: ("Level09_FrameCallback", "Level09_EnterRoutine"),
        0x001B653E: ("Level09_ExitCallback", "Level09_ExitRoutine"),
        0x001B623A: ("Level10_FrameCallback", "Level10_EnterRoutine"),
        0x001B6554: ("Level10_ExitCallback", "Level10_ExitRoutine"),
        0x001B6258: ("Level11_FrameCallback", "Level11_EnterRoutine"),
        0x001B655C: ("Level11_ExitCallback", "Level11_ExitRoutine"),
        0x001B62B6: ("Level12_FrameCallback", "Level12_EnterRoutine"),
        0x001B6562: ("Level12_ExitCallback", "Level12_ExitRoutine"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.aliases == (alias,)


def test_terrain_handler_actions_have_semantic_names_and_legacy_aliases():
    expected = {
        0x001B5318: ("TerrainHandler_SetTerminalCollisionFlag", "TerrainHandler_SetTerminalCollisionBlock"),
        0x001B5320: ("TerrainHandler_ApplySurfaceInteraction", "TerrainHandler_SurfaceInteractionBlock"),
        0x001B537A: ("TerrainHandler_ApplyLandingResponse", "TerrainHandler_LandingResponseBlock"),
        0x001B53A2: ("TerrainHandler_PushPlayerLeft", "TerrainHandler_HorizontalResponseBlock"),
        0x001B5492: ("TerrainHandler_ClearSurfaceMode", "TerrainHandler_ClearSurfaceModeBlock"),
        0x001B549C: ("TerrainHandler_SetSurfaceMode", "TerrainHandler_SetSurfaceModeBlock"),
        0x001B54E0: ("TerrainHandler_SetTerrainState", "TerrainHandler_SetTerrainStateBlock"),
        0x001B5502: ("TerrainHandler_StopAndAlignPlayer", "TerrainHandler_StopAndAlignPlayerBlock"),
        0x001B557E: ("TerrainHandler_LaunchPlayer", "TerrainHandler_LaunchPlayerBlock"),
        0x001B55E8: ("TerrainHandler_SnapPlayerToGrid", "TerrainHandler_SnapToGridBlock"),
        0x001B56B6: ("TerrainHandler_BouncePlayer", "TerrainHandler_BouncePlayerBlock"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.aliases == (alias,)
        assert symbol.confidence == "confirmed"

    finding = json.loads(
        Path("re/mame/findings/20260831-terrain-handler-identities-semantic-v1.json")
        .read_text(encoding="utf-8")
    )
    assert finding["promoted_symbol_count"] == len(expected)
    assert finding["status"] == "recorded-semantic-promotion"


def test_type84_base_interaction_spawn_handlers_have_semantic_names_and_aliases():
    expected = {
        0x001B70F8: ("InteractionSpawn_CreateInteractionBaseCollisionGatePrimary", "InteractionSpawn_CreateType84Base_B6"),
        0x001B712C: ("InteractionSpawn_CreateInteractionBaseCollisionGateSecondary", "InteractionSpawn_CreateType84Base_B7"),
        0x001B7158: ("InteractionSpawn_CreateInteractionBaseCollisionGateTertiary", "InteractionSpawn_CreateType84Base_B8"),
        0x001B717C: ("InteractionSpawn_CreateInteractionBasePlayerInteractionPrimary", "InteractionSpawn_CreateType84Base_B9"),
        0x001B71A0: ("InteractionSpawn_CreateInteractionBasePlayerInteractionSecondary", "InteractionSpawn_CreateType84Base_BA"),
        0x001B71C4: ("InteractionSpawn_CreateInteractionBaseUnconditional", "InteractionSpawn_CreateType84Base_CA"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_interaction_base_response_family_names_preserve_selector_aliases():
    expected = {
        0x001242B0: ("ACTOR_ANIM_INTERACTION_BASE_COLLISION_GATE_PRIMARY", "ACTOR_ANIM_TYPE84_BASE_B6"),
        0x001242CA: ("ACTOR_ANIM_INTERACTION_BASE_COLLISION_GATE_SECONDARY", "ACTOR_ANIM_TYPE84_BASE_B7"),
        0x001242E4: ("ACTOR_ANIM_INTERACTION_BASE_COLLISION_GATE_TERTIARY", "ACTOR_ANIM_TYPE84_BASE_B8"),
        0x001242FE: ("ACTOR_ANIM_INTERACTION_BASE_PLAYER_INTERACTION_PRIMARY", "ACTOR_ANIM_TYPE84_BASE_B9"),
        0x00124318: ("ACTOR_ANIM_INTERACTION_BASE_PLAYER_INTERACTION_SECONDARY", "ACTOR_ANIM_TYPE84_BASE_BA"),
        0x00124332: ("ACTOR_ANIM_INTERACTION_BASE_PLAYER_INTERACTION_UNCONDITIONAL", "ACTOR_ANIM_TYPE84_BASE_CA"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.metadata["type"] == "animation_stream"


def test_early_interaction_spawn_family_has_semantic_names_and_aliases():
    expected = {
        0x001B65C0: ("InteractionSpawn_CreateType84WithYOffset10", "InteractionSpawn_Type84OffsetY10"),
        0x001B65D4: ("InteractionSpawn_CreateType84TerminalActor", "InteractionSpawn_Type84Terminal"),
        0x001B65E0: ("InteractionSpawn_CreateType32WithYOffset8", "InteractionSpawn_Type32OffsetY8"),
        0x001B65F4: ("InteractionGate_CreateType62ByScene", "InteractionGate_Type62ByScene"),
        0x001B65FE: ("InteractionSpawn_CreateType62ByScene", "InteractionSpawn_Type62ByScene"),
        0x001B6622: ("InteractionSpawn_CreateType64WithXOffset16", "InteractionSpawn_Type64OffsetX16"),
        0x001B6636: ("InteractionSpawn_CreateType1AActorWithXYOffset16", "InteractionSpawn_Type1AOffset"),
        0x001B6654: ("InteractionSpawn_CreateType1BActorWithFacingAndXYOffset16", "InteractionSpawn_Type1BOffset"),
        0x001B6672: ("InteractionSpawn_CreateType1CActorWithYOffset16", "InteractionSpawn_Type1COffset"),
        0x001B668A: ("InteractionSpawn_CreateType58Actor", "InteractionSpawn_Type58"),
        0x001B6696: ("InteractionSpawn_CreateType58ActorWithAlternateAnimation", "InteractionSpawn_Type58AlternateAnimation"),
        0x001B66AC: ("InteractionSpawn_CreateType55AtOffsetX8Y0", "InteractionSpawn_Type55OffsetY0"),
        0x001B66C0: ("InteractionSpawn_CreateType55AtOffsetX8Y4", "InteractionSpawn_Type55OffsetY4"),
        0x001B66D8: ("InteractionSpawn_CreateType55AtOffsetX8Y8", "InteractionSpawn_Type55OffsetY8"),
        0x001B66F2: ("InteractionSpawn_CreateType55AtOffsetX8Y12", "InteractionSpawn_Type55OffsetY12"),
        0x001B670C: ("InteractionSpawn_CreateType74AtOffsetXMinus8Y4", "InteractionSpawn_Type74Offset"),
        0x001B6726: ("InteractionSpawn_CreateType07MovingInteractionActor", "InteractionSpawn_Type07"),
        0x001B6732: ("InteractionSpawn_CreateType8CLandingResponseActor", "InteractionSpawn_Type8C"),
        0x001B673E: ("InteractionSpawn_CreateType76ViaLevelObjectSpawn", "InteractionSpawn_Type76"),
        0x001B674A: ("InteractionSpawn_CreateType74TerrainResponseActor", "InteractionSpawn_Type74"),
        0x001B6756: ("InteractionSpawn_CreateType84TerrainResponseByScene", "InteractionSpawn_Type84TerrainResponseByScene"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence in {"confirmed", "decompiled", "trace_validated"}


def test_mid_interaction_spawn_family_has_semantic_names_and_aliases():
    expected = {
        0x001B67C2: ("InteractionSpawn_CreateType89RandomPair", "InteractionSpawn_Type89RandomPair"),
        0x001B6802: ("InteractionSpawn_CreateType6A6BAtOffsetX8YMinus1", "InteractionSpawn_Type6A6BOffset"),
        0x001B681C: ("InteractionSpawn_CreateType69AtOffsetX8YMinus1", "InteractionSpawn_Type69Offset"),
        0x001B6836: ("InteractionSpawn_CreateType6CResponseActor", "InteractionSpawn_Type6C"),
        0x001B6864: ("InteractionSpawn_CreateType23Actor", "InteractionSpawn_Type23"),
        0x001B6870: ("InteractionSpawn_CreateType06AtOffsetX9Y7", "InteractionSpawn_Type06Offset"),
        0x001B688A: ("InteractionSpawn_CreateType2BInteractionActor", "InteractionSpawn_Type2B"),
        0x001B6896: ("InteractionSpawn_CreateType84AtOffsetX20YMinus1", "InteractionSpawn_Type84OffsetYMinus1"),
        0x001B68B0: ("InteractionSpawn_CreateType84AtOffsetX11Y6", "InteractionSpawn_Type84OffsetY6"),
        0x001B68CA: ("InteractionSpawn_CreateType84ResponseActor", "InteractionSpawn_Type84Response"),
        0x001B68D6: ("InteractionGate_CreateType5FWhenReady", "InteractionSpawn_Type5FWhenReady"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_type5e84_pair_interaction_spawn_family_has_semantic_names_and_aliases():
    expected = {
        0x001B68F6: ("InteractionSpawn_CreateType5E84Pair_E0", "InteractionSpawn_Type5E84Pair_E0"),
        0x001B694C: ("InteractionSpawn_CreateType5E84Pair_E1E2", "InteractionSpawn_Type5E84Pair_E1E2"),
        0x001B6A00: ("InteractionSpawn_CreateType5E84Pair_E3E5", "InteractionSpawn_Type5E84Pair_E3E5"),
        0x001B6A5A: ("InteractionSpawn_CreateType5E84Pair_E8", "InteractionSpawn_Type5E84Pair_E8"),
        0x001B6AB4: ("InteractionSpawn_CreateType5E84Pair_E9", "InteractionSpawn_Type5E84Pair_E9"),
        0x001B6B0E: ("InteractionSpawn_CreateType5E84Pair_E6", "InteractionSpawn_Type5E84Pair_E6"),
        0x001B6B5C: ("InteractionSpawn_CreateType5E84Pair_F9WhenReady", "InteractionSpawn_Type5E84Pair_F9WhenReady"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_type5e_threshold_interaction_spawn_family_has_semantic_names_and_aliases():
    expected = {
        0x001B6BB2: ("InteractionSpawn_CreateType5EAtThreshold2646", "InteractionSpawn_Type5E_Threshold2646"),
        0x001B6BD8: ("InteractionSpawn_CreateType29ForSelectorsC0ThroughC7", "InteractionSpawn_Type29_C0C7"),
        0x001B6BE4: ("InteractionSpawn_CreateType29ForSelector57", "InteractionSpawn_Type29_57"),
        0x001B6BF0: ("InteractionSpawn_CreateType67ForSelector3D", "InteractionSpawn_Type67_3D"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_response_interaction_spawn_family_has_semantic_names_and_aliases():
    expected = {
        0x001B6C0E: ("InteractionSpawn_CreateType05VariantA_23_24", "InteractionSpawn_Type05VariantA_23_24"),
        0x001B6C2E: ("InteractionSpawn_CreateType05VariantC_25", "InteractionSpawn_Type05VariantC_25"),
        0x001B6C4E: ("InteractionSpawn_CreateType03_46", "InteractionSpawn_Type03_46"),
        0x001B6C5A: ("InteractionSpawn_CreateType10WithAudio_18", "InteractionSpawn_Type10WithAudio_18"),
        0x001B6C96: ("InteractionSpawn_CreateType2F_2A", "InteractionSpawn_Type2F_2A"),
        0x001B6CA2: ("InteractionSpawn_CreateType2FWithFacing_29", "InteractionSpawn_Type2F_Facing_29"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_type11_12_interaction_spawn_handlers_have_semantic_names_and_aliases():
    expected = {
        0x001B6CB4: ("InteractionSpawn_CreateType11WithPalette_1F", "InteractionSpawn_Type11WithPalette_1F"),
        0x001B6CCE: ("InteractionSpawn_CreateType12WithPresentation_4D", "InteractionSpawn_Type12WithPresentation_4D"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_type8b_presentation_handlers_have_semantic_names_and_aliases():
    expected = {
        0x001B6D90: ("InteractionSpawn_CreateType8BPresentation_F5", "InteractionSpawn_Type8BPresentation_F5"),
        0x001B6DEC: ("InteractionSpawn_CreateType8BPresentation_F6", "InteractionSpawn_Type8BPresentation_F6"),
        0x001B6E18: ("InteractionSpawn_CreateType8BPresentation_F7", "InteractionSpawn_Type8BPresentation_F7"),
        0x001B6E44: ("InteractionSpawn_CreateType8BPresentation_F8", "InteractionSpawn_Type8BPresentation_F8"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_type13_type14_interaction_spawn_handlers_have_semantic_names_and_aliases():
    expected = {
        0x001B6D1E: ("InteractionSpawn_CreateType13Interaction_28", "InteractionSpawn_Type13_28"),
        0x001B6D84: ("InteractionSpawn_CreateType14Interaction_13", "InteractionSpawn_Type14_13"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_upper_gated_interaction_spawn_handlers_have_semantic_names_and_aliases():
    expected = {
        0x001B6E7A: ("InteractionSpawn_CreateType20UpperActor_12", "InteractionSpawn_UpperTemplate_12"),
        0x001B6E86: ("InteractionGate_CreateType1EUpperActor_64", "InteractionGate_Type1E_64"),
        0x001B6E9C: ("InteractionGate_CreateType1FUpperActor_63", "InteractionGate_Type1F_63"),
        0x001B6EA6: ("InteractionSpawn_CreateType1FUpperActor_10", "InteractionSpawn_Type1F_10"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_type1d_type1e_interaction_spawn_handlers_have_semantic_names_and_aliases():
    expected = {
        0x001B6E70: ("InteractionGate_CreateType1DActor_65", "InteractionGate_Type1D", "confirmed"),
        0x001B6E90: ("InteractionSpawn_CreateType1EActor_11", "InteractionSpawn_Type1E", "confirmed"),
    }
    symbols = SymbolStore()
    for address, (name, alias, confidence) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == confidence


def test_type46_interaction_spawn_gate_has_semantic_name_and_alias():
    symbol = SymbolStore().at(0x001B742A, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "InteractionGate_CreateType46Actor_AF"
    assert "InteractionGate_Type46_AF" in symbol.aliases
    assert symbol.confidence == "decompiled"


def test_type79_type7a_interaction_spawn_handlers_have_semantic_names_and_aliases():
    expected = {
        0x001B6F1E: ("InteractionSpawn_CreateType79InteractionActor_0A0C", "InteractionSpawn_Type79_0A0C"),
        0x001B6F34: ("InteractionSpawn_CreateType7AInteractionActor_06", "InteractionSpawn_Type7A_06"),
        0x001B6F4A: ("InteractionSpawn_CreateType7AInteractionActor_07", "InteractionSpawn_Type7A_07"),
        0x001B6F60: ("InteractionSpawn_CreateType7AInteractionActor_08", "InteractionSpawn_Type7A_08"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_late_level_object_interaction_spawn_family_has_semantic_names_and_aliases():
    expected = {
        0x001B6F76: ("InteractionSpawn_CreateGuardActor_14", "InteractionSpawn_GuardTemplate_14"),
        0x001B6F82: ("InteractionSpawn_CreateType0BLevelEvent_3E", "InteractionSpawn_Type0B_3E"),
        0x001B6FAE: ("InteractionSpawn_CreateType0CLevelEvent_3C", "InteractionSpawn_Type0C_3C"),
        0x001B6FE2: ("InteractionSpawn_CreateType0FInteraction_26", "InteractionSpawn_Type0F_26"),
        0x001B6FEE: ("InteractionSpawn_CreateType0FInteractionFacing_16", "InteractionSpawn_Type0F_Facing_16"),
        0x001B7000: ("InteractionSpawn_CreateType16LevelObject_1C", "InteractionSpawn_Type16_1C"),
        0x001B700C: ("InteractionSpawn_CreateType36Interaction_EA", "InteractionSpawn_Type36_EA"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_collision_cluster_promotions_have_semantic_names_and_legacy_aliases():
    expected = {
        0x001AF1AC: ("PlayerCollision_SpawnVerticalResponseActor", "ActorType13_PlayerCollisionHandler"),
        0x001ACD54: ("ActorCollision_PrepareRecoveryPlane", "ActorType1E_PrepareRecoveryPlane"),
        0x001AF4A0: ("PlayerCollision_ReinitializeResponseActor", "ActorType33_38_39_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_collision_dispatch_entries_have_behavior_names_and_legacy_aliases():
    expected = {
        0x001CC2: ("PLAYER_COLLISION_HANDLER_PROXIMITY_BOUNCE", "PLAYER_COLLISION_HANDLER_TYPE_01"),
        0x001CC6: ("PLAYER_COLLISION_HANDLER_GROUND_CONTACT", "PLAYER_COLLISION_HANDLER_TYPE_02"),
        0x001CE6: ("PLAYER_COLLISION_HANDLER_SHARED_ACTOR_RESPONSE", "PLAYER_COLLISION_HANDLER_TYPE_0A"),
        0x001D6A: ("PLAYER_COLLISION_HANDLER_NOOP", "PLAYER_COLLISION_HANDLER_TYPE_2B"),
        0x001D6E: ("PLAYER_COLLISION_HANDLER_RESPONSE_PAIR", "PLAYER_COLLISION_HANDLER_TYPE_2C"),
        0x001D72: ("PLAYER_COLLISION_HANDLER_ACTION_RESPONSE", "PLAYER_COLLISION_HANDLER_TYPE_2D"),
        0x001D8A: ("PLAYER_COLLISION_HANDLER_STANDARD_RESPONSE", "PLAYER_COLLISION_HANDLER_TYPE_33"),
        0x001D8E: ("PLAYER_COLLISION_HANDLER_WALL_RESPONSE_REPLACEMENT", "PLAYER_COLLISION_HANDLER_TYPE_34"),
        0x001D9A: ("PLAYER_COLLISION_HANDLER_TIMED_RESPONSE", "PLAYER_COLLISION_HANDLER_TYPE_37"),
        0x001DC2: ("PLAYER_COLLISION_HANDLER_WALL_RESPONSE_SCENE_GATE", "PLAYER_COLLISION_HANDLER_TYPE_41"),
        0x001DC6: ("PLAYER_COLLISION_HANDLER_PRIMARY_COUNTER", "PLAYER_COLLISION_HANDLER_TYPE_42"),
        0x001E12: ("PLAYER_COLLISION_HANDLER_SURFACE_CONTACT", "PLAYER_COLLISION_HANDLER_TYPE_55"),
        0x001E1E: ("PLAYER_COLLISION_HANDLER_SURFACE_CONTACT_OFFSET", "PLAYER_COLLISION_HANDLER_TYPE_58"),
        0x001E26: ("PLAYER_COLLISION_HANDLER_LOWER_SURFACE", "PLAYER_COLLISION_HANDLER_TYPE_5A"),
        0x001E2A: ("PLAYER_COLLISION_HANDLER_UPPER_SURFACE", "PLAYER_COLLISION_HANDLER_TYPE_5B"),
        0x001E36: ("PLAYER_COLLISION_HANDLER_ADOPT_ACTOR_POSITION", "PLAYER_COLLISION_HANDLER_TYPE_5E"),
        0x001E3A: ("PLAYER_COLLISION_HANDLER_VERTICAL_CONTACT", "PLAYER_COLLISION_HANDLER_TYPE_5F"),
        0x001E3E: ("PLAYER_COLLISION_HANDLER_EXIT_PRESENTATION_UPDATE", "PLAYER_COLLISION_HANDLER_TYPE_60"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases
        assert symbol.confidence in {"confirmed", "decompiled"}


def test_actor_collision_dispatch_entries_have_behavior_names_and_legacy_aliases():
    expected = {
        0x001EBE: ("ACTOR_COLLISION_HANDLER_NOOP", "ACTOR_COLLISION_HANDLER_TYPE_01"),
        0x001EC2: ("ACTOR_COLLISION_HANDLER_CLEAR_SOURCE_AND_HANDLE", "ACTOR_COLLISION_HANDLER_TYPE_02"),
        0x001EC6: ("ACTOR_COLLISION_HANDLER_TERMINAL_RESPONSE_SETUP", "ACTOR_COLLISION_HANDLER_TYPE_03"),
        0x001ECA: ("ACTOR_COLLISION_HANDLER_DEATH_RESPONSE", "ACTOR_COLLISION_HANDLER_TYPE_04"),
        0x001ECE: ("ACTOR_COLLISION_HANDLER_INTERACTION_MOVEMENT_RESPONSE", "ACTOR_COLLISION_HANDLER_TYPE_05"),
        0x001EFA: ("ACTOR_COLLISION_HANDLER_STAGED_TERMINAL_RESPONSE", "ACTOR_COLLISION_HANDLER_TYPE_10"),
        0x001EFE: ("ACTOR_COLLISION_HANDLER_WALL_TERMINAL_RESPONSE", "ACTOR_COLLISION_HANDLER_TYPE_11"),
        0x001F06: ("ACTOR_COLLISION_HANDLER_INTERACTION_RESPONSE", "ACTOR_COLLISION_HANDLER_TYPE_13"),
        0x001F22: ("ACTOR_COLLISION_HANDLER_FACING_ALIGNED_INTERACTION", "ACTOR_COLLISION_HANDLER_TYPE_1A"),
        0x001F26: ("ACTOR_COLLISION_HANDLER_TERRAIN_INTERACTION_LATCH_A", "ACTOR_COLLISION_HANDLER_TYPE_1B"),
        0x001F2A: ("ACTOR_COLLISION_HANDLER_TERRAIN_INTERACTION_LATCH_B", "ACTOR_COLLISION_HANDLER_TYPE_1C"),
        0x001F36: ("ACTOR_COLLISION_HANDLER_UPPER_RESPONSE_PRIMARY", "ACTOR_COLLISION_HANDLER_TYPE_1F"),
        0x001F42: ("ACTOR_COLLISION_HANDLER_UPPER_RESPONSE_SECONDARY", "ACTOR_COLLISION_HANDLER_TYPE_22"),
        0x001F46: ("ACTOR_COLLISION_HANDLER_SPAWN_RESPONSE_PAIR", "ACTOR_COLLISION_HANDLER_TYPE_23"),
        0x001F4A: ("ACTOR_COLLISION_HANDLER_SPAWN_COMPANION_PRIMARY", "ACTOR_COLLISION_HANDLER_TYPE_24"),
        0x001F4E: ("ACTOR_COLLISION_HANDLER_SPAWN_COMPANION_SECONDARY", "ACTOR_COLLISION_HANDLER_TYPE_25"),
        0x001F52: ("ACTOR_COLLISION_HANDLER_ENTER_RESPONSE_STATE", "ACTOR_COLLISION_HANDLER_TYPE_26"),
        0x001F56: ("ACTOR_COLLISION_HANDLER_SPAWN_MOVEMENT_COMPANION", "ACTOR_COLLISION_HANDLER_TYPE_27"),
        0x001F5A: ("ACTOR_COLLISION_HANDLER_SPAWN_POSITIONED_COMPANION", "ACTOR_COLLISION_HANDLER_TYPE_28"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases
        assert symbol.confidence in {"confirmed", "decompiled"}


def test_interaction_and_level_streams_have_behavior_names_and_legacy_aliases():
    expected = {
        0x001204E2: ("ACTOR_MOVE_PROXIMITY_TRANSITION_GATE", "ACTOR_MOVE_TYPE1E_PROXIMITY_TRANSITION_GATE"),
        0x00120B36: ("ACTOR_MOVE_COMPACT_INTERACTION_RESPONSE", "ACTOR_MOVE_TYPE64_INTERACTION"),
        0x00120DE0: ("ACTOR_MOVE_LONG_INTERACTION_RESPONSE", "ACTOR_MOVE_TYPE53_INTERACTION"),
        0x00120FDE: ("ACTOR_MOVE_LEVEL_EVENT_INTERACTION", "ACTOR_MOVE_TYPE17_INTERACTION"),
        0x00121412: ("ACTOR_MOVE_WALL_RESPONSE_PREFIX", "ACTOR_MOVE_TYPE84_0F22_WALL_RESPONSE_PREFIX"),
        0x001227C2: ("PLAYER_ANIM_COLLISION_RESPONSE", "PLAYER_ANIM_TYPE1E_COLLISION_RESPONSE"),
        0x0012312C: ("ACTOR_ANIM_DIRECT_SPAWN_INTERACTION", "ACTOR_ANIM_TYPE5F_INTERACTION"),
        0x00123200: ("ACTOR_ANIM_VERTICAL_BOB_INTERACTION", "ACTOR_ANIM_TYPE06_INTERACTION"),
        0x00124226: ("ACTOR_ANIM_TERRAIN_RESPONSE", "ACTOR_ANIM_TYPE84_TERRAIN_RESPONSE"),
        0x00124616: ("ACTOR_ANIM_RESOURCE_INTERACTION_RESPONSE", "ACTOR_ANIM_TYPE32_INTERACTION"),
        0x00124CD8: ("ACTOR_ANIM_COMPACT_TERMINAL_INTERACTION", "ACTOR_ANIM_TYPE64_INTERACTION"),
        0x001251A6: ("ACTOR_ANIM_INTERACTION_PROXIMITY_VARIANT_A", "ACTOR_ANIM_TYPE05_INTERACTION_VARIANT_A"),
        0x001251FC: ("ACTOR_ANIM_INTERACTION_PROXIMITY_VARIANT_B", "ACTOR_ANIM_TYPE05_INTERACTION_VARIANT_B"),
        0x00125228: ("ACTOR_ANIM_INTERACTION_PROXIMITY_VARIANT_C", "ACTOR_ANIM_TYPE05_INTERACTION_VARIANT_C"),
        0x001256EE: ("ACTOR_ANIM_INTERACTION_RESPONSE_CHILD_SPAWN", "ACTOR_ANIM_TYPE53_INTERACTION"),
        0x00125952: ("ACTOR_ANIM_LEVEL_EVENT_DIRECTIONAL", "ACTOR_ANIM_TYPE0C_LEVEL_EVENT"),
        0x001259C0: ("ACTOR_ANIM_LEVEL_EVENT_CHILD_CHAIN", "ACTOR_ANIM_TYPE0B_LEVEL_EVENT"),
        0x00125D58: ("ACTOR_ANIM_WALL_RESPONSE_LOOP", "ACTOR_ANIM_TYPE84_0F22_WALL_RESPONSE_LOOP"),
        0x00129312: ("LEVEL07_EVENT_PALETTE_VARIANT", "INTERACTION_TYPE7D_PALETTE_VARIANT_0660"),
        0x001B8304: ("ACTOR_TEMPLATE_WALL_RESPONSE_VARIANT", "ACTOR_TEMPLATE_TYPE_84_0F22_WALL_RESPONSE"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases
        assert symbol.confidence in {"confirmed", "decompiled"}


def test_type23_actor_collision_pair_helpers_have_semantic_names_and_aliases():
    expected = {
        0x001ABFFA: ("ActorCollision_SpawnType23ResponsePair", "ActorType23_SpawnCollisionResponsePair"),
        0x001AC0BA: ("ActorCollision_SpawnZeroTemplateAtSourcePosition", "Actor_SpawnZeroTemplateAtSourcePosition"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_shared_actor_collision_services_have_semantic_names_and_aliases():
    expected = {
        0x001ABE8A: ("ActorCollision_ReinitializeFromInteractionTemplate", "Actor_HandleType2DInteraction"),
        0x001AC484: ("ActorCollision_ApplyTerminalResponse", "Actor_ApplyTerminalCollisionResponse"),
    }
    symbols = SymbolStore()
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence in {"confirmed", "decompiled"}


def test_actor_collision_response_cluster_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AC1D0: ("ActorCollision_ProcessType13Response", "ActorType13_ActorCollisionHandler"),
        0x001AC4E8: ("ActorCollision_HandleWallTerminalResponse", "ActorType11_ActorCollisionHandler"),
        0x001AEA48: ("PlayerCollision_HandleInteractionProximityResponse", "ActorType18_19_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_collision_transition_cluster_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AC102: ("ActorCollision_HandleDeathResponse", "ActorType04_ActorCollisionHandler"),
        0x001AEE40: ("PlayerCollision_HandleActionResponse", "ActorType2D_PlayerCollisionHandler"),
        0x001AFE1C: ("PlayerCollision_ProcessSceneTransition", "ActorType7E_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_compact_player_response_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AF344: ("PlayerCollision_ApplyTimedResponse", "ActorType37_3C_PlayerCollisionHandler"),
        0x001AF384: ("PlayerCollision_ApplyCountedGateResponse", "ActorType3D_PlayerCollisionHandler"),
        0x001AF3C2: ("PlayerCollision_ApplyWallResponseWithSceneGate", "ActorType41_PlayerCollisionHandler"),
        0x001AF4D8: ("PlayerCollision_ReplaceWithWallResponse", "ActorType34_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_secondary_player_response_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AEDA8: ("PlayerCollision_StageType84ActionResponse", "ActorType2F_PlayerCollisionHandler"),
        0x001AF228: ("PlayerCollision_AdvanceSecondaryResponse", "ActorType3A_PlayerCollisionHandler"),
        0x001AF110: ("PlayerCollision_ProcessInteractionResponse", "ActorType11_12_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_counter_and_interaction_response_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AC350: ("ActorCollision_ProcessType20Interaction", "ActorType20_ActorCollisionHandler"),
        0x001AF264: ("PlayerCollision_AdvancePrimaryCounter", "ActorType42_PlayerCollisionHandler"),
        0x001AF400: ("PlayerCollision_SpawnType29Response", "ActorType29_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_response_difficulty_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AE64C: ("PlayerCollision_ActivateType43Interaction", "ActorType43_PlayerCollisionHandler"),
        0x001AEF12: ("PlayerCollision_ApplyDifficultyResponseStep", "ActorType44_PlayerCollisionHandler"),
        0x001AEEE0: ("PlayerCollision_CommitPendingResponse", "ActorType45_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_transition_alignment_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AFB36: ("PlayerCollision_AlignToTerrainActor", "ActorType6E_73_PlayerCollisionHandler"),
        0x001AFC4E: ("PlayerCollision_StartTransitionBounce", "ActorType4F_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_type1b_1c_death_response_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AEA00: ("PlayerCollision_SpawnType1BDeathResponse", "ActorType1B_PlayerCollisionHandler"),
        0x001AEA24: ("PlayerCollision_SpawnType1CDeathResponse", "ActorType1C_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_collision_latch_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AEFB0: ("PlayerCollision_LatchType47Gate", "ActorType47_PlayerCollisionHandler"),
        0x001AEFDC: ("PlayerCollision_LatchType48Gate", "ActorType48_PlayerCollisionHandler"),
        0x001AF008: ("PlayerCollision_LatchType49Gate", "ActorType49_PlayerCollisionHandler"),
        0x001AF034: ("PlayerCollision_LatchType4AInteraction", "ActorType4A_PlayerCollisionHandler"),
        0x001AF060: ("PlayerCollision_LatchType4BInteraction", "ActorType4B_PlayerCollisionHandler"),
        0x001AF08C: ("PlayerCollision_LatchType4CInteraction", "ActorType4C_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases
    latch = SymbolStore().at(0x00FFF116, include_ranges=False)
    assert latch is not None
    assert latch.name == "PLAYER_INTERACTION_FOLLOWUP_LATCH"
    assert "PLAYER_INTERACTION_TYPE4B_LATCH" in latch.aliases


def test_player_level_exit_response_has_behavior_name_and_legacy_alias():
    symbol = SymbolStore().at(0x001AFA84, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "PlayerCollision_StartLevelExitResponse"
    assert "ActorType74_75_PlayerCollisionHandler" in symbol.aliases


def test_compact_actor_player_response_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AC614: ("ActorCollision_ProcessType14Interaction", "ActorType14_ActorCollisionHandler"),
        0x001AC63C: ("ActorCollision_StageType84EventResponse", "ActorType2B_ActorCollisionHandler"),
        0x001AEE18: ("PlayerCollision_ReinitializeType30Response", "ActorType30_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_compact_actor_response_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AC03E: ("ActorCollision_SpawnType40CompanionFromType24", "ActorType24_ActorCollisionHandler"),
        0x001AC05A: ("ActorCollision_SpawnType40CompanionFromType25", "ActorType25_ActorCollisionHandler"),
        0x001AC07C: ("ActorCollision_SpawnType46CompanionFromType27", "ActorType27_ActorCollisionHandler"),
        0x001AC098: ("ActorCollision_SpawnType84CompanionFromType28", "ActorType28_ActorCollisionHandler"),
        0x001AC682: ("ActorCollision_ReinitializeType30Response", "ActorType30_ActorCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_type3e_3f_response_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AF2B0: ("PlayerCollision_StartType3EResponse", "ActorType3E_PlayerCollisionHandler"),
        0x001AF2FA: ("PlayerCollision_StartType3FResponse", "ActorType3F_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_type62_63_settle_has_behavior_name_and_legacy_alias():
    symbol = SymbolStore().at(0x001AF81C, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "PlayerCollision_SettleType62Response"
    assert "ActorType62_63_PlayerCollisionHandler" in symbol.aliases


def test_player_type15_1a_response_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AE978: ("PlayerCollision_DecrementPrimaryCounterResponse", "ActorType15_PlayerCollisionHandler"),
        0x001AE9E0: ("PlayerCollision_SpawnType1ADeathResponse", "ActorType1A_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_directional_bounce_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AFCD2: ("PlayerCollision_StartDirectionalBounce", "ActorType4E_PlayerCollisionHandler"),
        0x001AFD32: ("PlayerCollision_QueueDirectionalBounceAudio", "ActorType4E_QueueRandomResponseAudio"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_followup_response_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AF7F2: ("PlayerCollision_TriggerTerminalTransition", "ActorType54_PlayerCollisionHandler"),
        0x001AFF82: ("PlayerCollision_ResolveType02GroundContact", "ActorType02_PlayerCollisionHandler"),
        0x001AE9A8: ("PlayerCollision_ReinitializeType0CResponse", "ActorType0C_PlayerCollisionHandler"),
        0x001AEF5C: ("PlayerCollision_AdvanceDifficultyCounter", "ActorType46_PlayerCollisionHandler"),
        0x001AC432: ("ActorCollision_TriggerType1BInteraction", "ActorType1B_ActorCollisionHandler"),
        0x001AC444: ("ActorCollision_TriggerType1CInteraction", "ActorType1C_ActorCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_position_response_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AF638: ("PlayerCollision_AdoptActorPosition", "ActorType5E_PlayerCollisionHandler"),
        0x001AFD84: ("PlayerCollision_StartProximityBounce", "ActorType01_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_actor_collision_response_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001ABF8E: ("ActorCollision_ClearSourceAndHandleType02_08_09", "ActorType02_08_09_ActorCollisionHandler"),
        0x001ABFF0: ("ActorCollision_StartType23ResponsePair", "ActorType23_ActorCollisionHandler"),
        0x001AC2BC: ("ActorCollision_AdvanceType10Response", "ActorType10_ActorCollisionHandler"),
        0x001AC676: ("ActorCollision_ClearSourceAndHandleType2F", "ActorType2F_ActorCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_terrain_exit_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AE722: ("PlayerCollision_ArmDirectionalTerrainPush", "ActorType08_09_PlayerCollisionHandler"),
        0x001AF0B8: ("PlayerCollision_ProcessType4DPairedResponse", "ActorType4D_PlayerCollisionHandler"),
        0x001AF6DC: ("PlayerCollision_UpdateExitPresentationActor", "ActorType60_61_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_contact_state_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AF978: ("PlayerCollision_ResolveHandholdContact", "ActorType6A_PlayerCollisionHandler"),
        0x001AEB7C: ("PlayerCollision_EnterInteractionState", "ActorType32_79_PlayerCollisionHandler"),
        0x001AF8F6: ("PlayerCollision_EnterTransitionContact", "ActorType50_51_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_surface_bounce_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AFBF4: ("PlayerCollision_StartBounceActorResponse", "ActorType65_PlayerCollisionHandler"),
        0x001AF53E: ("PlayerCollision_SetLowerSurfaceResponse", "ActorType5A_PlayerCollisionHandler"),
        0x001AF54A: ("PlayerCollision_SetUpperSurfaceResponse", "ActorType5B_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_interaction_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AC408: ("ActorCollision_ProcessType1AInteraction", "ActorType1A_ActorCollisionHandler"),
        0x001AEBA4: ("PlayerCollision_ProcessCameraOrTerminalInteraction", "ActorType7D_PlayerCollisionHandler"),
        0x001AEBDC: ("PlayerCollision_ProcessInteractionProximity", "ActorType78_7A_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_response_cleanup_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AEECA: ("PlayerCollision_ActivateType23ResponsePair", "ActorType23_PlayerCollisionHandler"),
        0x001AF556: ("PlayerCollision_CleanupType08Actors", "ActorType5C_PlayerCollisionHandler"),
        0x001AF562: ("PlayerCollision_CleanupType09Actors", "ActorType5D_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_actor_response_setup_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AC076: ("ActorCollision_EnterType84Response", "ActorType26_ActorCollisionHandler"),
        0x001AC0EE: ("ActorCollision_InstallType03Response", "ActorType03_ActorCollisionHandler"),
        0x001AC1B4: ("ActorCollision_InstallType05Response", "ActorType05_ActorCollisionHandler"),
        0x001AC2E0: ("ActorCollision_InstallType1FResponse", "ActorType1F_ActorCollisionHandler"),
        0x001AC2FC: ("ActorCollision_InstallType22Response", "ActorType22_ActorCollisionHandler"),
        0x001AC318: ("ActorCollision_InstallType1EResponse", "ActorType1E_ActorCollisionHandler"),
        0x001AC334: ("ActorCollision_InstallType21Response", "ActorType21_ActorCollisionHandler"),
        0x001AC4DE: ("ActorCollision_PromoteSourceAndToggleFacing", "ActorType18_19_ActorCollisionHandler"),
        0x001AC60E: ("ActorCollision_ToggleFacing", "ActorType0D_ActorCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_player_terrain_contact_family_has_behavior_names_and_legacy_aliases():
    expected = {
        0x001AF516: ("PlayerCollision_CreateType36EffectActor", "ActorType36_PlayerCollisionHandler"),
        0x001AF590: ("PlayerCollision_ResolveType55_57SurfaceContact", "ActorType55_56_57_PlayerCollisionHandler"),
        0x001AF5F0: ("PlayerCollision_ResolveType58SurfaceContact", "ActorType58_PlayerCollisionHandler"),
        0x001AF6AC: ("PlayerCollision_ResolveType5FVerticalContact", "ActorType5F_PlayerCollisionHandler"),
        0x001AF740: ("PlayerCollision_ResolveType67_68Contact", "ActorType67_68_PlayerCollisionHandler"),
        0x001AF79E: ("PlayerCollision_ApplyType52_53Offset", "ActorType52_53_PlayerCollisionHandler"),
        0x001AF894: ("PlayerCollision_ApplyType64Offset", "ActorType64_PlayerCollisionHandler"),
        0x001AF9F6: ("PlayerCollision_ResolveType76_77TransitionContact", "ActorType76_77_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_final_player_and_actor_dispatch_entries_have_canonical_names():
    expected = {
        0x001AED86: ("PlayerCollision_HandleType03Interaction", "ActorType03_PlayerCollisionHandler"),
        0x001AF21E: ("PlayerCollision_GateType3BResponse", "ActorType3B_PlayerCollisionHandler"),
        0x001ABF9A: ("ActorCollision_NoopType01", "ActorType01_ActorCollisionHandler"),
        0x001AC6A2: ("ActorCollision_ConvertType2D2E31Response", "ActorType2D2E31_ActorCollisionHandler"),
        0x001AEB7A: ("PlayerCollision_NoopType0D", "ActorType0D_PlayerCollisionHandler"),
        0x001AEBFE: ("PlayerCollision_NoopType2B", "ActorType2B_PlayerCollisionHandler"),
        0x001AEDA6: ("PlayerCollision_NoopType04", "ActorType04_PlayerCollisionHandler"),
        0x001AEEDE: ("PlayerCollision_NoopType24_28", "ActorType24_28_PlayerCollisionHandler"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_remaining_data_identities_have_stable_names_and_legacy_aliases():
    expected = {
        0x00128E4B: ("SCENE_RESOURCE_TILE_BASE_2000_COMMAND", "SCENE_RESOURCE_TILE_BASE_2000_COMMAND_128E4B"),
        0x00128EB2: ("PALETTE_UNIFORM_0EEE", "PALETTE_ALL_0EEE_SOURCE_128EB2"),
        0x00129312: ("LEVEL07_EVENT_PALETTE_VARIANT", "PALETTE_BAND_VARIANT_OF_INTERACTION_TYPE7D_129312"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_vm_stream_promotions_and_spawn_handoff_marker_have_stable_names():
    symbols = SymbolStore()
    streams = {
        0x0011F800: ("ACTOR_MOVE_INTERACTION_ANCHOR_GRID_RESPONSE", "ACTOR_MOVE_UNREFERENCED_GRID_RESPONSE_11F800"),
        0x00123DE2: ("ACTOR_ANIM_SINGLE_FRAME_SELF_LOOP", "ACTOR_ANIM_UNREFERENCED_SELF_LOOP_123DE2"),
    }
    for address, (name, legacy) in streams.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases

    marker = symbols.at(0x00FFF0E5, include_ranges=False)
    assert marker is not None
    assert marker.name == "INTERACTION_SPAWN_HANDOFF_MARKER"
    assert marker.metadata["format"] == "boolean"


def test_unresolved_vm_stream_runtime_finding_keeps_negative_evidence_bounded():
    finding = json.loads(
        Path("re/mame/findings/20260831-unresolved-vm-streams-runtime-negative-v1.json")
        .read_text(encoding="utf-8")
    )
    assert finding["status"] == "recorded-runtime-negative-evidence"
    assert finding["route"]["frames"] == 2296
    assert finding["route"]["rom_read_count"] == 0
    assert finding["route"]["checkpoints_reached"][-1] == "frontier-end"
    assert len(finding["ranges"]) == 2
    assert all(item["read_count"] == 0 for item in finding["ranges"])
    assert any("not an exhaustive reachability proof" in item for item in finding["limitations"])


def test_actor_vm_orphan_objects_have_behavior_names_and_legacy_aliases():
    expected = {
        0x001203E8: ("ACTOR_MOVE_DRIFT_PLUS2_PLUS1_LOOP", "ACTOR_MOVE_UNREFERENCED_SELF_LOOP_1203E8"),
        0x001B7990: ("ACTOR_TEMPLATE_TYPE84_RESOURCE10_NO_DEFAULT_VM", "ACTOR_TEMPLATE_TYPE_84_UNREFERENCED_RESOURCE10"),
        0x001B7A58: ("ACTOR_TEMPLATE_TYPE84_EMPTY_INIT_40001400", "ACTOR_TEMPLATE_TYPE_84_UNREFERENCED_PAYLOAD_40001400"),
    }
    symbols = SymbolStore()
    for address, (name, legacy) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases
        assert symbol.confidence == "decompiled"


def test_actor_vm_orphan_runtime_finding_covers_all_five_ranges():
    finding = json.loads(
        Path("re/mame/findings/20260831-actor-vm-orphans-natural-route-runtime-negative-v1.json")
        .read_text(encoding="utf-8")
    )
    assert finding["route"]["frames"] == 2296
    assert finding["route"]["rom_read_count"] == 0
    assert finding["route"]["route_frontier"] == "Level 01 upper-edge frontier at frame 2296"
    assert len(finding["ranges"]) == 5
    assert all(item["read_count"] == 0 for item in finding["ranges"])
    assert all(len(item["range"]) == 2 for item in finding["ranges"])
    assert any("not an exhaustive reachability proof" in item for item in finding["limitations"])


def test_actor_vm_orphan_long_route_finding_records_duration_without_overclaiming():
    finding = json.loads(
        Path("re/mame/findings/20260831-actor-vm-orphans-long-route-runtime-negative-v1.json")
        .read_text(encoding="utf-8")
    )
    assert finding["route"]["scenario"] == "long-gameplay-traversal"
    assert finding["route"]["frames"] == 5000
    assert finding["route"]["final_scene_state"] == 1
    assert finding["route"]["rom_read_count"] == 0
    assert len(finding["ranges"]) == 5
    assert all(item["read_count"] == 0 for item in finding["ranges"])
    assert any("multi-scene coverage" in item for item in finding["evidence"])


def test_actor_vm_orphan_state08_finding_proves_transition_without_overclaiming():
    finding = json.loads(
        Path("re/mame/findings/20260831-actor-vm-orphans-state08-runtime-negative-v1.json")
        .read_text(encoding="utf-8")
    )
    assert finding["route"]["scenario"] == "controlled-state08-selector"
    assert finding["route"]["frames"] == 1900
    assert finding["route"]["rom_read_count"] == 0
    assert "SCENE_STATE changed from 0x01 to 0x08" in finding["route"]["rom_owned_transition"]
    assert len(finding["ranges"]) == 5
    assert all(item["read_count"] == 0 for item in finding["ranges"])
    assert any("not exhaustive" in item for item in finding["limitations"])


def test_actor_vm_orphan_transition_checkpoints_keep_reachability_bounded():
    finding = json.loads(
        Path("re/mame/findings/20260831-actor-vm-orphans-transition-checkpoints-runtime-negative-v1.json")
        .read_text(encoding="utf-8")
    )
    assert finding["status"] == "recorded-runtime-negative-evidence"
    assert finding["route"]["checkpoint_count"] == 6
    assert finding["route"]["total_frame_limit"] == 720
    assert finding["route"]["rom_read_count"] == 0
    assert {item["final_scene_state"] for item in finding["route"]["checkpoints"]} == {1, 3}
    assert len(finding["ranges"]) == 4
    assert all(item["read_count"] == 0 for item in finding["ranges"])
    assert any("not an exhaustive" in item for item in finding["limitations"])


def test_scene_graphics_resources_have_stable_state_and_destination_names():
    expected = {
        0x0012DA04: ("SCENE_STATE04_C000_GRAPHICS", "SCENE_RNC_GRAPHICS_0012DA04"),
        0x0012E176: ("SCENE_STATE01_C000_GRAPHICS_SECONDARY", "SCENE_RNC_GRAPHICS_0012E176"),
        0x0012E34A: ("SCENE_STATE01_C000_GRAPHICS_PRIMARY", "SCENE_RNC_GRAPHICS_0012E34A"),
        0x0012F39E: ("SCENE_DISPATCH_E000_GRAPHICS", "SCENE_RNC_GRAPHICS_0012F39E"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_rebuild_and_active_scene_graphics_have_stable_destination_names():
    expected = {
        0x0012F712: ("SCENE_REBUILD_C000_GRAPHICS", "SCENE_RNC_GRAPHICS_0012F712"),
        0x0012FA02: ("SCENE_REBUILD_E000_GRAPHICS", "SCENE_RNC_GRAPHICS_0012FA02"),
        0x0012FF0F: ("SCENE_ACTIVE_STATE01_C000_GRAPHICS", "SCENE_RNC_GRAPHICS_0012FF0F"),
        0x0013013C: ("SCENE_ACTIVE_STATE01_E000_GRAPHICS", "SCENE_RNC_GRAPHICS_0013013C"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_presentation_graphics_have_stable_consumer_names():
    expected = {
        0x0013046F: ("SCENE_ACTIVE_C000_GRAPHICS", "SCENE_RNC_GRAPHICS_0013046F"),
        0x001313FD: ("SCENE_STATE_PRESENTATION_C000_STANDARD_GRAPHICS", "SCENE_STATE_RNC_GRAPHICS_001313FD"),
        0x00131682: ("SCENE_STATE_PRESENTATION_C000_SPECIAL_GRAPHICS", "SCENE_STATE_RNC_GRAPHICS_00131682"),
        0x00131830: ("SCENE_SCRIPT_TRANSITION_C000_GRAPHICS", "SCENE_TRANSITION_RNC_GRAPHICS_00131830"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_title_and_menu_graphics_have_stable_consumer_names():
    expected = {
        0x00130709: ("MENU_WISH_PROMPT_GRAPHICS", "MENU_WISH_PROMPT_GRAPHICS_00130709"),
        0x001307D5: ("TITLE_E000_GRAPHICS", "TITLE_RNC_GRAPHICS_001307D5"),
        0x00130EA1: ("TITLE_C000_GRAPHICS_PRIMARY", "TITLE_RNC_GRAPHICS_00130EA1"),
        0x00131020: ("TITLE_C000_GRAPHICS_SECONDARY", "TITLE_RNC_GRAPHICS_00131020"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_shared_base_graphics_have_stable_consumer_names():
    expected = {
        0x001319EC: ("SCENE_PRESENTATION_COMMON_BASE_GRAPHICS", "SCENE_COMMON_BASE_RNC_GRAPHICS_001319EC"),
        0x0013A892: ("SCENE_REBUILD_BASE_GRAPHICS", "SCENE_REBUILD_BASE_RNC_GRAPHICS_0013A892"),
        0x0013C374: ("SCENE_GLOBAL_SHARED_BASE_GRAPHICS", "SCENE_SHARED_RNC_GRAPHICS_0013C374"),
        0x001401DA: ("TITLE_BASE_GRAPHICS", "TITLE_BASE_RNC_GRAPHICS_001401DA"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_scene_presentation_streams_have_stable_state_names():
    expected = {
        0x001270A8: ("SCENE_RESOURCE_PRESENTATION_STREAM_STATE_01_PRIMARY", "SCENE_RESOURCE_PRESENTATION_STREAM_12E34A_1270A8"),
        0x00127134: ("SCENE_RESOURCE_PRESENTATION_STREAM_STATE_01_SECONDARY", "SCENE_RESOURCE_PRESENTATION_STREAM_12E176_127134"),
        0x00127207: ("SCENE_RESOURCE_PRESENTATION_STREAM_STATE_03", "SCENE_RESOURCE_PRESENTATION_STREAM_12DD76_127207"),
        0x00127338: ("SCENE_RESOURCE_PRESENTATION_STREAM_STATE_00", "SCENE_RESOURCE_PRESENTATION_STREAM_12DD76_127338"),
        0x001273E9: ("SCENE_RESOURCE_PRESENTATION_STREAM_STATE_04", "SCENE_RESOURCE_PRESENTATION_STREAM_12DA04_1273E9"),
        0x00127571: ("SCENE_RESOURCE_PRESENTATION_STREAM_STATE_05_PRIMARY", "SCENE_RESOURCE_PRESENTATION_STREAM_12DD76_127571"),
        0x001275EE: ("SCENE_RESOURCE_PRESENTATION_STREAM_STATE_05_SECONDARY", "SCENE_RESOURCE_PRESENTATION_STREAM_12DD76_1275EE"),
        0x0012772D: ("SCENE_RESOURCE_PRESENTATION_STREAM_STATE_07", "SCENE_RESOURCE_PRESENTATION_STREAM_12D870_12772D"),
        0x001277C5: ("SCENE_RESOURCE_BLANK_STREAM_STATE_07", "SCENE_RESOURCE_BLANK_STREAM_1277C5"),
        0x00127B60: ("SCENE_RESOURCE_BLANK_STREAM_STATE_04_PRELUDE", "SCENE_RESOURCE_BLANK_STREAM_127B60"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_shared_scene_resources_have_stable_consumer_names():
    expected = {
        0x0012622E: ("SCENE_TRANSITION_MODE_PRESENTATION_STREAM", "SCENE_TRANSITION_PRESENTATION_STREAM_12622E"),
        0x0013030B: ("SCENE_ACTIVE_RESET_SHARED_GRAPHICS", "SCENE_RNC_GRAPHICS_0013030B"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_scene_palette_sources_have_stable_graphics_consumer_names():
    expected = {
        0x00129912: ("SCENE_RESOURCE_PALETTE_BAND0_FOR_STATE07_C000_GRAPHICS", "SCENE_RESOURCE_PALETTE_BAND0_SOURCE_129912"),
        0x00129932: ("SCENE_RESOURCE_PALETTE_BAND0_FOR_STATE04_C000_GRAPHICS", "SCENE_RESOURCE_PALETTE_BAND0_SOURCE_129932"),
        0x00129952: ("SCENE_RESOURCE_PALETTE_BAND0_FOR_SHARED_C000_GRAPHICS", "SCENE_RESOURCE_PALETTE_BAND0_SOURCE_129952"),
        0x00129972: ("SCENE_RESOURCE_PALETTE_BAND0_FOR_STATE0B_C000_GRAPHICS", "SCENE_RESOURCE_PALETTE_BAND0_SOURCE_129972"),
        0x00129992: ("SCENE_RESOURCE_PALETTE_BAND0_FOR_STATE01_SECONDARY_C000_GRAPHICS", "SCENE_RESOURCE_PALETTE_BAND0_SOURCE_129992"),
        0x001299B2: ("SCENE_RESOURCE_PALETTE_BAND0_FOR_STATE01_PRIMARY_C000_GRAPHICS", "SCENE_RESOURCE_PALETTE_BAND0_SOURCE_1299B2"),
        0x00129A92: ("SCENE_RESOURCE_PALETTE_BAND1_FOR_SHARED_STATE01_C000_GRAPHICS", "SCENE_RESOURCE_PALETTE_BAND1_SOURCE_129A92"),
        0x00129AB2: ("SCENE_RESOURCE_PALETTE_BAND1_FOR_STATE07_04_0B_C000_GRAPHICS", "SCENE_RESOURCE_PALETTE_BAND1_SOURCE_129AB2"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_direct_palette_sources_have_stable_loader_names():
    expected = {
        0x00129012: ("MENU_OPTIONS_PALETTE_BAND2_SOURCE", "MENU_PALETTE_BAND_SOURCE_129012"),
        0x001292B2: ("INTERACTION_SHARED_PALETTE_SOURCE", "INTERACTION_PALETTE_SOURCE_1292B2"),
        0x00129812: ("SCENE_RESOURCE_PALETTE_BAND2_FOR_C000_RESOURCE_VARIANT", "SCENE_RESOURCE_PALETTE_BAND2_SOURCE_129812"),
        0x001298D2: ("SCENE_RESOURCE_PALETTE_BAND2_FOR_COMMON_BASE_RESOURCE", "SCENE_RESOURCE_PALETTE_BAND2_SOURCE_1298D2"),
        0x001298F2: ("SCENE_RESOURCE_PALETTE_BAND2_FOR_E000_RESOURCE_PAIR_A", "SCENE_RESOURCE_PALETTE_BAND2_SOURCE_1298F2"),
        0x001299D2: ("SCENE_RESOURCE_PALETTE_BAND2_FOR_E000_RESOURCE_PAIR_B", "SCENE_RESOURCE_PALETTE_BAND2_SOURCE_1299D2"),
        0x00129A12: ("SCENE_RESOURCE_PALETTE_BAND2_FOR_COMMON_VRAM_PAIR", "SCENE_RESOURCE_PALETTE_BAND2_SOURCE_129A12"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_type84_scene_templates_and_terminal_animation_have_stable_names():
    expected = {
        0x00122FA2: ("ACTOR_ANIM_GUARD_SWORD_TERMINAL_DEATH", "ACTOR_ANIM_DEATH_122FA2"),
        0x0012609E: ("ACTOR_ANIM_SCENE_RESOURCE_OPENING", "ACTOR_ANIM_TYPE84_SCENE_RESOURCE_OPENING"),
        0x001260DA: ("ACTOR_ANIM_SCENE_REBUILD", "ACTOR_ANIM_TYPE84_SCENE_REBUILD"),
        0x001260EA: ("ACTOR_ANIM_SCENE_REBUILD_ENTRY", "ACTOR_ANIM_TYPE84_SCENE_REBUILD_ENTRY"),
        0x001B83E0: ("ACTOR_TEMPLATE_SCENE_RESOURCE_OPENING", "ACTOR_TEMPLATE_SCENE_RESOURCE_TYPE_84_12609E"),
        0x001B83F4: ("ACTOR_TEMPLATE_SCENE_RESOURCE_REBUILD", "ACTOR_TEMPLATE_SCENE_RESOURCE_TYPE_84_1260EA"),
        0x001B8408: ("ACTOR_TEMPLATE_SCENE_RESOURCE_COLLISION_RESPONSE", "ACTOR_TEMPLATE_SCENE_RESOURCE_TYPE_84_123E7E"),
        0x001B841C: ("ACTOR_TEMPLATE_SCENE_RESOURCE_RESPONSE", "ACTOR_TEMPLATE_SCENE_RESOURCE_TYPE_84_123F7E"),
        0x001B8430: ("ACTOR_TEMPLATE_SCENE_REBUILD_PRIMARY", "ACTOR_TEMPLATE_TYPE_84_SCENE_REBUILD_A"),
        0x001B8444: ("ACTOR_TEMPLATE_SCENE_REBUILD_WALL", "ACTOR_TEMPLATE_TYPE_84_SCENE_REBUILD_WALL"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_level_event_names_promote_known_level05_level07_roles():
    symbols = SymbolStore()
    expected = {
        0x00121180: ("ACTOR_MOVE_LEVEL_EVENT_WIDE_RANDOM_OFFSETS_PRELUDE", "ACTOR_MOVE_TYPE7C_WIDE_RANDOM_OFFSETS_PRELUDE"),
        0x00125916: ("ACTOR_ANIM_LEVEL_EVENT_SHARED", "ACTOR_ANIM_TYPE7C_LEVEL_EVENT_SHARED"),
        0x001B819C: ("ACTOR_TEMPLATE_LEVEL05_TIMED_SPAWN", "ACTOR_TEMPLATE_TYPE_7C_LEVEL05_TIMED_SPAWN"),
        0x001B81B0: ("ACTOR_TEMPLATE_WIDE_RANDOM_EVENT", "ACTOR_TEMPLATE_TYPE_7C_WIDE_RANDOM_EVENT"),
        0x001B81C4: ("ACTOR_TEMPLATE_LEVEL07_CALLBACK_SPAWN", "ACTOR_TEMPLATE_TYPE_7C_LEVEL07_CALLBACK_SPAWN"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases

    level_events = next(mapping for mapping in load_entity_mappings() if mapping.name == "LEVEL_EVENT_SHARED")
    assert set(expected) - {0x00121180, 0x001B81B0} <= set(level_events.symbol_addresses)
    assert level_events.scope == "role"


def test_scene_resource_coordinate_stream_has_stable_name():
    symbol = SymbolStore().at(0x00006744, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "SCENE_RESOURCE_QUEUED_ACTOR_COORDINATES"
    assert "SCENE_RESOURCE_ACTOR_SPAWN_COORDINATES_6744" in symbol.aliases


def test_menu_palette_containers_have_stable_names():
    expected = {
        0x00128F52: ("MENU_OPTIONS_PALETTE_BANK", "MENU_OPTIONS_PALETTE_BANK_128F52"),
        0x001296B2: ("MENU_OPTIONS_PALETTE_RECORD_BANK", "MENU_PALETTE_RECORD_BANK_1296B2"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_type4b_latch_is_closed_as_proven_unconsumed():
    function = SymbolStore().at(0x001AF060, include_ranges=False)
    latch = SymbolStore().at(0x00FFF116, include_ranges=False)
    assert function is not None
    assert latch is not None
    assert function.metadata["review_status"] == "closed"
    assert latch.metadata["review_status"] == "closed"


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


def test_real_hud_and_milestone_ranges_cover_interior_bytes():
    symbols = SymbolStore()
    expected = {
        0x00FF7E2D: "HUD_DISPLAY_DIGITS",
        0x00FF7E35: "INTERACTION_PENDING_DISPLAY_VALUE",
        0x00FF7E3A: "INTERACTION_RESOURCE_PROGRESS_COUNTER",
        0x00FF7E14: "INTERACTION_RESOURCE_MODE_1B_MILESTONE",
        0x00FF7E18: "INTERACTION_RESOURCE_MODE_1C_MILESTONE",
    }
    for address, name in expected.items():
        symbol = symbols.at(address)
        assert symbol is not None
        assert symbol.name == name


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
        0x00FFF0E4: "TERRAIN_RESPONSE_AUXILIARY_FLAG",
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

    brake = symbols.at(0x00FFF101, include_ranges=False)
    assert brake is not None
    assert "0x00122078" in brake.description
    assert "ED 01 F101 0001" in brake.description


def test_real_player_action_animation_state_has_recovered_vm_producers():
    symbols = SymbolStore()

    state = symbols.at(0x00FFF0DA, include_ranges=False)
    assert state is not None
    assert state.name == "PLAYER_ACTION_ANIMATION_STATE"
    assert "38 ED 01 F0DA 0001" in state.description
    assert "0x001AC7A2" in state.description


def test_real_level04_event45_has_animation_vm_producers():
    symbols = SymbolStore()

    event = symbols.at(0x00FFF12C, include_ranges=False)
    assert event is not None
    assert event.name == "LEVEL04_EVENT_45_PENDING"
    assert "ED 01 F12C 0001" in event.description
    assert "0x001253A6" in event.description


def test_real_level04_event38_has_exact_callback_clear_and_open_setter():
    symbols = SymbolStore()

    event = symbols.at(0x00FFF11A, include_ranges=False)
    assert event is not None
    assert event.name == "LEVEL04_EVENT_38_PENDING"
    assert "0x001B5BC6" in event.description
    assert "upstream setter is not present" in event.description


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


def test_real_actor_render_offsets_have_canonical_roles():
    symbols = SymbolStore()

    expected = {
        0x00FFF080: "ACTOR_RENDER_X_OFFSET",
        0x00FFF082: "ACTOR_RENDER_Y_OFFSET",
    }
    for address, name in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == "pixels"


def test_real_actor_movement_origins_have_canonical_roles():
    symbols = SymbolStore()

    expected = {
        0x00FFF090: "ACTOR_MOVEMENT_ORIGIN_X",
        0x00FFF092: "ACTOR_MOVEMENT_ORIGIN_Y",
    }
    for address, name in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == "pixels"


def test_real_level08_event_state_has_canonical_roles():
    symbols = SymbolStore()

    expected = {
        0x00FFF084: "LEVEL08_EVENT_PHASE",
        0x00FFF086: "LEVEL08_EVENT_COUNTER_HIGH",
        0x00FFF088: "LEVEL08_EVENT_COUNTER_LOW",
        0x00FFF08A: "LEVEL08_VDP_RECORD_OFFSET",
        0x00FFF12E: "LEVEL08_EVENT_COMMAND_CURSOR",
        0x00FFF132: "LEVEL_EVENT_SCRIPT_CURSOR",
    }
    for address, name in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name


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


def test_real_scene_setup_actor_slots_have_structural_canonical_roles():
    symbols = SymbolStore()

    expected = {
        0x00FF7F06: ("ACTOR_TABLE_SLOT_3_BASE", "address"),
        0x00FF7F08: ("ACTOR_SLOT_3_X", "pixels"),
        0x00FF7F0A: ("ACTOR_SLOT_3_Y", "pixels"),
        0x00FF7F48: ("ACTOR_TABLE_SLOT_4_BASE", "address"),
        0x00FF7F4A: ("ACTOR_SLOT_4_X", "pixels"),
        0x00FF7F4C: ("ACTOR_SLOT_4_Y", "pixels"),
    }
    for address, (name, value_format) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == value_format


def test_real_scene_rebuild_actor_slots_have_structural_canonical_roles():
    symbols = SymbolStore()

    expected = {
        0x00FF7F8A: ("ACTOR_TABLE_SLOT_5_BASE", "address", "confirmed"),
        0x00FF7F8C: ("ACTOR_SLOT_5_X", "pixels", "confirmed"),
        0x00FF7F8E: ("ACTOR_SLOT_5_Y", "pixels", "confirmed"),
        0x00FF7F94: ("ACTOR_SLOT_5_MOVEMENT_PC", "rom_pointer", "decompiled"),
        0x00FF7FCC: ("ACTOR_TABLE_SLOT_6_BASE", "address", "confirmed"),
        0x00FF7FCE: ("ACTOR_SLOT_6_X", "pixels", "confirmed"),
        0x00FF7FD0: ("ACTOR_SLOT_6_Y", "pixels", "confirmed"),
        0x00FF7FD6: ("ACTOR_SLOT_6_MOVEMENT_PC", "rom_pointer", "decompiled"),
        0x00FF800E: ("ACTOR_TABLE_SLOT_7_BASE", "address", "confirmed"),
        0x00FF8010: ("ACTOR_SLOT_7_X", "pixels", "confirmed"),
        0x00FF8012: ("ACTOR_SLOT_7_Y", "pixels", "confirmed"),
        0x00FF8018: ("ACTOR_SLOT_7_MOVEMENT_PC", "rom_pointer", "decompiled"),
    }
    for address, (name, value_format, confidence) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == value_format
        assert symbol.confidence == confidence


def test_real_scene_rebuild_actor_animation_cursors_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FF7FAA: "ACTOR_SLOT_5_ANIMATION_PC",
        0x00FF7FEC: "ACTOR_SLOT_6_ANIMATION_PC",
        0x00FF802E: "ACTOR_SLOT_7_ANIMATION_PC",
    }
    for address, name in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == "rom_pointer"
        assert symbol.confidence == "decompiled"


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


def test_real_vdp_tile_row_command_tables_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FF8680: "VDP_TILE_ROW_COMMAND_TABLE_SELECTED",
        0x00FF8700: "VDP_TILE_ROW_COMMAND_TABLE_SELECTED_MIRROR",
        0x00FF8780: "VDP_TILE_ROW_COMMAND_TABLE_ALTERNATE",
    }
    for address, name in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == 0x80
        assert symbol.metadata["format"] == "vdp_command_table"
        assert symbol.confidence == "decompiled"


def test_real_renderer_and_counter_word_ranges_cover_low_byte_references():
    symbols = SymbolStore()
    expected = {
        0x00FFEFE1: "INTERACTION_COUNTER_DIGITS",
        0x00FFEFE3: "INTERACTION_COUNTER_SECONDARY_DIGITS",
        0x00FFEFEF: "ACTOR_SPRITE_PAYLOAD_COUNT",
    }
    for address, name in expected.items():
        symbol = symbols.at(address)
        assert symbol is not None
        assert symbol.name == name


def test_real_actor_sprite_payload_buffer_has_canonical_role():
    symbol = SymbolStore().at(0x00FF769A, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "RENDER_ACTOR_SPRITE_PAYLOAD_BUFFER_BASE"
    assert symbol.kind == "ram"
    assert symbol.metadata["format"] == "address"
    assert symbol.confidence == "confirmed"


def test_real_interaction_response_flag_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF104, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "INTERACTION_RESPONSE_FLAG"
    assert symbol.kind == "ram"
    assert symbol.metadata["format"] == "boolean"
    assert symbol.confidence == "decompiled"
    assert "no direct reader" in symbol.description


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


def test_real_scene_resource_service_marker_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF158, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "SCENE_RESOURCE_SERVICE_MARKER"
    assert symbol.metadata["format"] == "boolean"
    assert symbol.confidence == "decompiled"
    assert "no direct reader" in symbol.description


def test_real_scene_resource_rebuild_phase_counter_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF122, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "SCENE_RESOURCE_REBUILD_PHASE_COUNTER"
    assert symbol.metadata["format"] == "countdown"
    assert symbol.confidence == "decompiled"


def test_real_scene_resource_rebuild_phase_state_has_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FFF121: ("SCENE_RESOURCE_REBUILD_PHASE_DELAY", "countdown"),
        0x00FFF120: ("SCENE_RESOURCE_REBUILD_WAIT_GATE", "boolean"),
        0x00FFF11E: ("SCENE_RESOURCE_REBUILD_PHASE_PENDING", "boolean"),
    }
    for address, (name, format_name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["type"] == "u8"
        assert symbol.metadata["format"] == format_name
        assert symbol.confidence == "decompiled"


def test_real_scene_resource_fast_path_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF11B, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "SCENE_RESOURCE_FAST_PATH"
    assert symbol.metadata["type"] == "u8"
    assert symbol.metadata["format"] == "boolean"
    assert symbol.confidence == "decompiled"


def test_real_global_prng_state_has_canonical_role():
    symbol = SymbolStore().at(0x00FF7DEA, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "GLOBAL_PRNG_STATE"
    assert symbol.metadata["type"] == "u32"
    assert symbol.metadata["format"] == "prng_state"
    assert symbol.confidence == "decompiled"


def test_real_palette_transition_source_has_canonical_role():
    symbol = SymbolStore().at(0x00FF7DF2, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "PALETTE_TRANSITION_SOURCE"
    assert symbol.metadata["type"] == "rom_pointer"
    assert symbol.metadata["format"] == "address"
    assert symbol.confidence == "decompiled"


def test_real_game_runtime_mode_has_canonical_role():
    symbol = SymbolStore().at(0x00FF7E20, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "GAME_RUNTIME_MODE"
    assert symbol.metadata["type"] == "u8"
    assert symbol.metadata["format"] == "mode"
    assert symbol.confidence == "decompiled"


def test_real_hud_display_nonzero_seen_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF0FE, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "HUD_DISPLAY_NONZERO_SEEN"
    assert symbol.metadata["type"] == "u8"
    assert symbol.metadata["format"] == "boolean"
    assert symbol.confidence == "decompiled"


def test_real_interaction_row_pointer_has_canonical_role():
    symbol = SymbolStore().at(0x00FF7DAC, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "INTERACTION_ROW_POINTER"
    assert symbol.metadata["type"] == "rom_pointer"
    assert symbol.metadata["format"] == "address"
    assert symbol.confidence == "decompiled"


def test_real_hud_interaction_frame_cursor_has_canonical_role():
    symbol = SymbolStore().at(0x00FF7DA8, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "HUD_INTERACTION_FRAME_CURSOR"
    assert symbol.metadata["type"] == "rom_pointer"
    assert symbol.metadata["format"] == "address"
    assert symbol.confidence == "decompiled"


def test_real_interaction_resource_milestones_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FF7E12: "INTERACTION_RESOURCE_MODE_1B_MILESTONE",
        0x00FF7E16: "INTERACTION_RESOURCE_MODE_1C_MILESTONE",
    }
    for address, name in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["type"] == "u8[4]"
        assert symbol.metadata["format"] == "ascii_milestone"
        assert symbol.confidence == "decompiled"


def test_real_interaction_coordinate_scratch_has_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FF7DB0: ("INTERACTION_HANDLER_X", "coordinate"),
        0x00FF7DB2: ("INTERACTION_HANDLER_Y", "coordinate"),
        0x00FFF150: ("INTERACTION_SPAWN_X_OFFSET", "coordinate_offset"),
        0x00FFF152: ("INTERACTION_SPAWN_Y_OFFSET", "coordinate_offset"),
    }
    for address, (name, format_name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["type"] == "u16"
        assert symbol.metadata["format"] == format_name
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
    assert type3e.name == "PLAYER_COLLISION_FOLLOWUP_RESPONSE_GATE_PRIMARY"
    assert "PLAYER_COLLISION_GATE_TYPE3E" in type3e.aliases
    assert type3e.metadata["format"] == "boolean"
    assert type3f is not None
    assert type3f.name == "PLAYER_COLLISION_FOLLOWUP_RESPONSE_GATE_SECONDARY"
    assert "PLAYER_COLLISION_GATE_TYPE3F" in type3f.aliases
    assert type3f.metadata["format"] == "boolean"


def test_real_type47_49_collision_gates_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FFF126: "PLAYER_COLLISION_BASE_RESPONSE_GATE_PRIMARY",
        0x00FFF127: "PLAYER_COLLISION_BASE_RESPONSE_GATE_SECONDARY",
        0x00FFF128: "PLAYER_COLLISION_BASE_RESPONSE_GATE_TERTIARY",
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


def test_real_actor_sprite_payload_count_has_canonical_role():
    symbol = SymbolStore().at(0x00FFEFEE, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "ACTOR_SPRITE_PAYLOAD_COUNT"
    assert symbol.metadata["type"] == "u16"
    assert symbol.metadata["format"] == "record_count"


def test_real_palette_transition_step_has_canonical_role():
    symbol = SymbolStore().at(0x00FFEFF6, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "PALETTE_TRANSITION_STEP"
    assert symbol.metadata["type"] == "u16"
    assert symbol.metadata["format"] == "counter"


def test_real_level09_type50_spawn_cooldown_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF118, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "LEVEL09_PERIODIC_SPAWN_COOLDOWN"
    assert "LEVEL09_TYPE50_SPAWN_COOLDOWN" in symbol.aliases
    assert symbol.metadata["type"] == "u8"
    assert symbol.metadata["format"] == "counter"


def test_real_level00_type13_palette_delay_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF124, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "LEVEL00_RESPONSE_PALETTE_DELAY"
    assert "LEVEL00_TYPE13_PALETTE_DELAY" in symbol.aliases
    assert symbol.metadata["type"] == "u8"
    assert symbol.metadata["format"] == "countdown"


def test_real_scene_state8_transition_count_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF006, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "SCENE_STATE8_TRANSITION_COUNT"
    assert symbol.metadata["type"] == "u8"
    assert symbol.metadata["format"] == "counter"
    assert symbol.confidence == "decompiled"


def test_real_actor_type42_collision_step_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF10A, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "PLAYER_COLLISION_CONTACT_STEP"
    assert "ACTOR_TYPE42_COLLISION_STEP" in symbol.aliases
    assert symbol.metadata["type"] == "u8"
    assert symbol.metadata["format"] == "counter"


def test_real_collision_and_presentation_state_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FFF10E: ("PLAYER_DEATH_RESPONSE_LATCH", "boolean"),
        0x00FFF10F: ("PLAYER_DEATH_RESPONSE_LATCH_VARIANT_A", "boolean"),
        0x00FFF110: ("PLAYER_DEATH_RESPONSE_LATCH_VARIANT_B", "boolean"),
        0x00FFF123: ("INTERACTION_PRESENTATION_ALTERNATE_LATCH", "boolean"),
        0x00FFF125: ("PLAYER_COLLISION_CONTACT_COUNT", "counter"),
        0x00FFF129: ("PLAYER_INTERACTION_BASE_RESPONSE_GATE_PRIMARY", "boolean"),
        0x00FFF12A: ("PLAYER_INTERACTION_BASE_RESPONSE_GATE_SECONDARY", "boolean"),
        0x00FFF13E: ("MENU_PRESENTATION_TIMEOUT", "countdown"),
        0x00FFF154: ("RAW_TERRAIN_QUERY_STATE", "boolean"),
        0x00FFF570: ("SCENE_PRESENTATION_LATCH", "boolean"),
        0x00FFF0A0: ("SCENE_RESOURCE_PRESENTATION_FILL_STATE", "counter"),
        0x00FFF109: ("LEVEL09_CALLBACK_PHASE", "counter"),
    }
    for address, (name, format_name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == format_name
        assert symbol.confidence == "decompiled"


def test_real_actor_slot_and_camera_callback_state_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FFF179: ("PLAYER_COLLISION_ALTERNATE_RESPONSE_LATCH", "boolean", "decompiled"),
        0x00FFF09E: ("CAMERA_SCROLL_RENDER_OFFSET", "integer", "decompiled"),
        0x00FF7E84: ("ACTOR_SLOT_1_X", "pixels", "confirmed"),
        0x00FF7E86: ("ACTOR_SLOT_1_Y", "pixels", "confirmed"),
    }
    for address, (name, format_name, confidence) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == format_name
        assert symbol.confidence == confidence


def test_remaining_animation_and_movement_roles_preserve_numeric_aliases():
    symbols = SymbolStore()
    expected = {
        0x001209F0: ("ACTOR_MOVE_RANDOM_STEP_POSITIVE", "ACTOR_MOVE_TYPE84_RANDOM_VARIANT_A"),
        0x001209F8: ("ACTOR_MOVE_RANDOM_STEP_NEGATIVE", "ACTOR_MOVE_TYPE84_RANDOM_VARIANT_B"),
        0x00124C3A: ("ACTOR_ANIM_SCENE_SETUP_MOVING_CHILD_VARIANT_LOOP", "ACTOR_ANIM_TYPE84_MOVING_CHILD_VARIANT_LOOP"),
        0x001250CE: ("ACTOR_ANIM_TERRAIN_SCENE5_VARIANT_A", "ACTOR_ANIM_TYPE84_TERRAIN_SCENE5_VARIANT_A"),
        0x001250DE: ("ACTOR_ANIM_TERRAIN_SCENE5_VARIANT_B", "ACTOR_ANIM_TYPE84_TERRAIN_SCENE5_VARIANT_B"),
        0x00125966: ("ACTOR_ANIM_DIRECTIONAL_RESPONSE", "ACTOR_ANIM_TYPE0C_DIRECTIONAL_RESPONSE"),
        0x00126118: ("ACTOR_ANIM_MENU_SECONDARY_PRESENTATION", "ACTOR_ANIM_MENU_TYPE84_SECONDARY"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases


def test_remaining_collision_roles_preserve_numeric_aliases():
    symbols = SymbolStore()
    latch = symbols.at(0x00FFF179, include_ranges=False)
    assert latch is not None
    assert latch.name == "PLAYER_COLLISION_ALTERNATE_RESPONSE_LATCH"
    assert "PLAYER_INTERACTION_TYPE3D_LATCH" in latch.aliases

    helper = symbols.at(0x001B69A6, include_ranges=False)
    assert helper is not None
    assert helper.name == "InteractionSpawn_CreatePairedAnchorResponse"
    assert "InteractionSpawn_Type5E84Pair_AnchorResponse" in helper.aliases


def test_real_player_actor_coordinates_and_menu_gate_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FF7E42: ("PLAYER_ACTOR_X", "pixels", "confirmed"),
        0x00FF7E44: ("PLAYER_ACTOR_Y", "pixels", "confirmed"),
        0x00FF7E46: ("PLAYER_ACTOR_BEHAVIOR_FLAGS", "bitfield", "decompiled"),
        0x00FF7E47: ("PLAYER_ACTOR_STATE_FLAGS", "bitfield", "decompiled"),
        0x00FF7E75: ("PLAYER_ACTOR_FACING_Y_FLIP", "boolean", "decompiled"),
        0x00FFF157: ("MENU_OPTIONS_PRESENTATION_GATE", "boolean", "decompiled"),
    }
    for address, (name, format_name, confidence) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == format_name
        assert symbol.confidence == confidence


def test_real_input_and_menu_phase_state_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FFEFFD: ("INPUT_EDGE_LATCH", "boolean", "decompiled"),
        0x00FFEFFE: ("MENU_CONTROLLER_POLL_GATE", "boolean", "decompiled"),
        0x00FF7274: ("MENU_OPTIONS_SUBPHASE", "counter", "decompiled"),
        0x00FF729A: ("RENDER_SPRITE_RECORD_BUFFER_BASE", "address", "confirmed"),
        0x00FFEFEC: ("RENDER_SPRITE_RECORD_COUNT", "record_count", "confirmed"),
    }
    for address, (name, format_name, confidence) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == format_name
        assert symbol.confidence == confidence


def test_real_actor_slot_one_fields_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FF7E88: ("ACTOR_SLOT_1_FLAGS", "bitfield", "confirmed"),
        0x00FF7E89: ("ACTOR_SLOT_1_STATE_FLAGS", "bitfield", "decompiled"),
        0x00FF7E8B: ("ACTOR_SLOT_1_FACING_X_FLIP", "boolean", "decompiled"),
        0x00FF7E8C: ("ACTOR_SLOT_1_MOVEMENT_PC", "rom_pointer", "decompiled"),
        0x00FF7EBE: ("ACTOR_SLOT_1_STATUS_FLAGS", "bitfield", "decompiled"),
        0x00FF7E96: ("ACTOR_SLOT_1_FRAME_PTR", "rom_pointer", "confirmed"),
        0x00FF7EA2: ("ACTOR_SLOT_1_ANIMATION_PC", "rom_pointer", "decompiled"),
    }
    for address, (name, format_name, confidence) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == format_name
        assert symbol.confidence == confidence


def test_real_actor_table_slot_24_base_has_canonical_role():
    symbol = SymbolStore().at(0x00FF8470, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "ACTOR_TABLE_SLOT_24_BASE"
    assert symbol.kind == "ram"
    assert symbol.metadata["format"] == "address"
    assert symbol.confidence == "confirmed"
    assert "slots 24 down to 1" in symbol.description


def test_real_actor_table_slot_20_base_has_canonical_role():
    symbol = SymbolStore().at(0x00FF8368, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "ACTOR_TABLE_SLOT_20_BASE"
    assert symbol.kind == "ram"
    assert symbol.metadata["format"] == "address"
    assert symbol.confidence == "confirmed"
    assert "slots 20 down to 1" in symbol.description


def test_real_actor_slot_two_fields_and_transition_option_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FF7ECA: ("ACTOR_SLOT_2_FLAGS", "bitfield", "confirmed"),
        0x00FF7ECB: ("ACTOR_SLOT_2_STATE_FLAGS", "bitfield", "decompiled"),
        0x00FF7ECD: ("ACTOR_SLOT_2_FACING_X_FLIP", "boolean", "decompiled"),
        0x00FF7ECE: ("ACTOR_SLOT_2_MOVEMENT_PC", "rom_pointer", "decompiled"),
        0x00FF7F00: ("ACTOR_SLOT_2_STATUS_FLAGS", "bitfield", "decompiled"),
        0x00FF7ED8: ("ACTOR_SLOT_2_FRAME_PTR", "rom_pointer", "confirmed"),
        0x00FF7EE4: ("ACTOR_SLOT_2_ANIMATION_PC", "rom_pointer", "decompiled"),
        0x00FF7273: ("SCENE_TRANSITION_OPTION_3_ENABLED", "boolean", "decompiled"),
    }
    for address, (name, format_name, confidence) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == format_name
        assert symbol.confidence == confidence


def test_real_player_terrain_transition_gate_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF114, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "PLAYER_TERRAIN_TRANSITION_GATE"
    assert symbol.metadata["type"] == "u8"
    assert symbol.metadata["format"] == "boolean"


def test_real_collision_geometry_and_event_latches_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FFF08C: ("PLAYER_COLLISION_X_LEFT", "pixels"),
        0x00FFF08E: ("PLAYER_COLLISION_X_RIGHT", "pixels"),
        0x00FFF0DA: ("PLAYER_ACTION_ANIMATION_STATE", "boolean"),
        0x00FFF0F6: ("PLAYER_COLLISION_CURRENT_ACTOR_TYPE", "integer"),
        0x00FFF10B: ("LEVEL_EVENT_PRESENTATION_STATE", "integer"),
        0x00FFF112: ("PLAYER_COLLISION_PAIR_SPAWN_GATE", "boolean"),
        0x00FFF11A: ("LEVEL04_EVENT_38_PENDING", "boolean"),
        0x00FFF11C: ("PLAYER_COLLISION_RESPONSE_SPAWN_GATE", "boolean"),
        0x00FFF11D: ("SURFACE_INTERACTION_BLOCK", "boolean"),
        0x00FFF12C: ("LEVEL04_EVENT_45_PENDING", "boolean"),
    }
    for address, (name, format_name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == format_name
        assert symbol.confidence == "decompiled"


def test_numeric_ram_identities_promote_behavior_roles_and_preserve_aliases():
    symbols = SymbolStore()
    expected = {
        0x00FFF105: ("UPPER_INTERACTION_LEVEL_OBJECT_GATE_PRIMARY", "INTERACTION_GATE_TYPE1F"),
        0x00FFF106: ("UPPER_INTERACTION_LEVEL_OBJECT_GATE_SECONDARY", "INTERACTION_GATE_TYPE1D"),
        0x00FFF107: ("UPPER_INTERACTION_UPPER_ROUTE_GATE", "INTERACTION_GATE_TYPE1E"),
        0x00FFF10A: ("PLAYER_COLLISION_CONTACT_STEP", "ACTOR_TYPE42_COLLISION_STEP"),
        0x00FFF10E: ("PLAYER_DEATH_RESPONSE_LATCH", "PLAYER_INTERACTION_TYPE1A_LATCH"),
        0x00FFF10F: ("PLAYER_DEATH_RESPONSE_LATCH_VARIANT_A", "PLAYER_INTERACTION_TYPE1B_LATCH"),
        0x00FFF110: ("PLAYER_DEATH_RESPONSE_LATCH_VARIANT_B", "PLAYER_INTERACTION_TYPE1C_LATCH"),
        0x00FFF112: ("PLAYER_COLLISION_PAIR_SPAWN_GATE", "PLAYER_COLLISION_TYPE18_19_PAIR_SPAWN_GATE"),
        0x00FFF116: ("PLAYER_INTERACTION_FOLLOWUP_LATCH", "PLAYER_INTERACTION_TYPE4B_LATCH"),
        0x00FFF118: ("LEVEL09_PERIODIC_SPAWN_COOLDOWN", "LEVEL09_TYPE50_SPAWN_COOLDOWN"),
        0x00FFF11C: ("PLAYER_COLLISION_RESPONSE_SPAWN_GATE", "PLAYER_COLLISION_TYPE29_SPAWN_GATE"),
        0x00FFF11D: ("SURFACE_INTERACTION_BLOCK", "ACTOR_TYPE11_SURFACE_INTERACTION_BLOCK"),
        0x00FFF123: ("INTERACTION_PRESENTATION_ALTERNATE_LATCH", "INTERACTION_TYPE12_PRESENTATION_LATCH"),
        0x00FFF124: ("LEVEL00_RESPONSE_PALETTE_DELAY", "LEVEL00_TYPE13_PALETTE_DELAY"),
        0x00FFF125: ("PLAYER_COLLISION_CONTACT_COUNT", "ACTOR_TYPE18_19_CONTACT_COUNT"),
        0x00FFF126: ("PLAYER_COLLISION_BASE_RESPONSE_GATE_PRIMARY", "PLAYER_COLLISION_GATE_TYPE47"),
        0x00FFF127: ("PLAYER_COLLISION_BASE_RESPONSE_GATE_SECONDARY", "PLAYER_COLLISION_GATE_TYPE48"),
        0x00FFF128: ("PLAYER_COLLISION_BASE_RESPONSE_GATE_TERTIARY", "PLAYER_COLLISION_GATE_TYPE49"),
        0x00FFF129: ("PLAYER_INTERACTION_BASE_RESPONSE_GATE_PRIMARY", "PLAYER_INTERACTION_TYPE4A_LATCH"),
        0x00FFF12A: ("PLAYER_INTERACTION_BASE_RESPONSE_GATE_SECONDARY", "PLAYER_INTERACTION_TYPE4C_LATCH"),
        0x00FFF16F: ("TERRAIN_RESPONSE_INTERACTION_GATE_SPECIAL_A", "INTERACTION_GATE_TYPE44"),
        0x00FFF170: ("TERRAIN_RESPONSE_INTERACTION_GATE_SPECIAL_B", "INTERACTION_GATE_TYPE3A"),
        0x00FFF171: ("TERRAIN_RESPONSE_INTERACTION_GATE_SPECIAL_C", "INTERACTION_GATE_TYPE40"),
        0x00FFF177: ("PLAYER_COLLISION_FOLLOWUP_RESPONSE_GATE_PRIMARY", "PLAYER_COLLISION_GATE_TYPE3E"),
        0x00FFF178: ("PLAYER_COLLISION_FOLLOWUP_RESPONSE_GATE_SECONDARY", "PLAYER_COLLISION_GATE_TYPE3F"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases


def test_template_roles_promote_known_producers_and_preserve_numeric_aliases():
    symbols = SymbolStore()
    expected = {
        0x001B7864: ("ACTOR_TEMPLATE_PLAYER_BOOTSTRAP", "ACTOR_TEMPLATE_TYPE_83_PLAYER_BOOT"),
        0x001B79F4: ("ACTOR_TEMPLATE_FRAME_PHASE_CHILD", "ACTOR_TEMPLATE_TYPE_00_FRAME_PHASE_CHILD"),
        0x001B7B98: ("ACTOR_TEMPLATE_SCENE_SETUP_SLOT_1", "ACTOR_TEMPLATE_TYPE_84_SCENE_SETUP_A"),
        0x001B7BAC: ("ACTOR_TEMPLATE_SCENE_SETUP_SLOT_2", "ACTOR_TEMPLATE_TYPE_84_SCENE_SETUP_B"),
        0x001B7BC0: ("ACTOR_TEMPLATE_SCENE_SETUP_SLOT_3", "ACTOR_TEMPLATE_TYPE_04_SCENE_SETUP"),
        0x001B7D3C: ("ACTOR_TEMPLATE_MENU_PRESENTATION_CHILD_PRIMARY", "ACTOR_TEMPLATE_MENU_TYPE_84_CHILD_A"),
        0x001B7D50: ("ACTOR_TEMPLATE_MENU_PRESENTATION_CHILD_SECONDARY", "ACTOR_TEMPLATE_MENU_TYPE_84_CHILD_B"),
        0x001B7CEC: ("ACTOR_TEMPLATE_COLLISION_WALL_RESPONSE_CHILD", "ACTOR_TEMPLATE_TYPE_84_WALL_RESPONSE_CHILD"),
        0x001B7D00: ("ACTOR_TEMPLATE_ACTOR_COLLISION_CHILD_EFFECT", "ACTOR_TEMPLATE_TYPE84_CHILD"),
        0x001B7E18: ("ACTOR_TEMPLATE_LEVEL_EXIT_PRESENTATION_CHILD", "ACTOR_TEMPLATE_EXIT_CHILD_TYPE8C"),
        0x001B7D28: ("ACTOR_TEMPLATE_MENU_PRESENTATION_PRIMARY", "ACTOR_TEMPLATE_MENU_TYPE_84"),
        0x001B7ECC: ("ACTOR_TEMPLATE_PLAYER_BOOTSTRAP_CHILD", "ACTOR_TEMPLATE_F5_TYPE_84"),
        0x001B7EE0: ("ACTOR_TEMPLATE_LEVEL08_ENTRY", "ACTOR_TEMPLATE_LEVEL08_TYPE_84"),
        0x001B7EB8: ("ACTOR_TEMPLATE_TERRAIN_EXIT_RESPONSE_CHILD", "ACTOR_TEMPLATE_TYPE_84_TYPE74_RESPONSE_CHILD"),
        0x001B7F08: ("ACTOR_TEMPLATE_MOVING_CHILD_BASE", "ACTOR_TEMPLATE_TYPE_84_F5_MOVING_CHILD_BASE"),
        0x001B7F58: ("ACTOR_TEMPLATE_PRESENTATION_CHILD_SECONDARY", "ACTOR_TEMPLATE_TYPE_30_PRESENTATION_CHILD"),
        0x001B828C: ("ACTOR_TEMPLATE_RUNTIME_EVENT_CHILD", "ACTOR_TEMPLATE_TYPE_84_RUNTIME47_4C_CHILD"),
        0x001B7FD0: ("ACTOR_TEMPLATE_INTERACTION_RESPONSE_DEFAULT", "ACTOR_TEMPLATE_TYPE_62_DEFAULT_RESPONSE"),
        0x001B7FE4: ("ACTOR_TEMPLATE_INTERACTION_RESPONSE_SCENE5", "ACTOR_TEMPLATE_TYPE_62_SCENE5_RESPONSE"),
        0x001B8020: ("ACTOR_TEMPLATE_LEVEL_OBJECT_VARIANT", "ACTOR_TEMPLATE_TYPE_16_LEVEL_OBJECT"),
        0x001B8070: ("ACTOR_TEMPLATE_MOVING_INTERACTION", "ACTOR_TEMPLATE_TYPE_07_MOVING_INTERACTION"),
        0x001B80C0: ("ACTOR_TEMPLATE_INTERACTION_VARIANT_SHARED", "ACTOR_TEMPLATE_TYPE_52"),
        0x001B814C: ("ACTOR_TEMPLATE_LEVEL_ENTRY_ACTOR", "ACTOR_TEMPLATE_TYPE_42_LEVEL_ENTRY"),
        0x001B81D8: ("ACTOR_TEMPLATE_LEVEL_ENTRY_VARIANT", "ACTOR_TEMPLATE_TYPE_7D"),
        0x001B8214: ("ACTOR_TEMPLATE_LEVEL11_EVENT", "ACTOR_TEMPLATE_TYPE_7B_LEVEL11_EVENT"),
        0x001B8188: ("ACTOR_TEMPLATE_LEVEL_EVENT_CHILD_OPENING", "ACTOR_TEMPLATE_TYPE_84_LEVEL_EVENT_CHILD"),
        0x001B8200: ("ACTOR_TEMPLATE_DIRECTIONAL_CHILD", "ACTOR_TEMPLATE_TYPE_2E_DIRECTIONAL_CHILD"),
        0x001B8124: ("ACTOR_TEMPLATE_MENU_FLAGGED_CHILD", "ACTOR_TEMPLATE_TYPE_84_F5_FLAGGED_CHILD"),
        0x001B83CC: ("ACTOR_TEMPLATE_LEVEL11_EVENT_CHILD", "ACTOR_TEMPLATE_TYPE_7B_RESPONSE_CHILD"),
        0x001B83A4: ("ACTOR_TEMPLATE_LEVEL_EVENT_RESPONSE_CHILD", "ACTOR_TEMPLATE_TYPE_84_LEVEL_EVENT_RESPONSE_CHILD"),
        0x001B8160: ("ACTOR_TEMPLATE_LEVEL_ENTRY_RESPONSE", "ACTOR_TEMPLATE_TYPE_54"),
        0x001B8340: ("ACTOR_TEMPLATE_SCENE_RESET", "ACTOR_TEMPLATE_TYPE_84_SCENE_RESET"),
        0x001B837C: ("ACTOR_TEMPLATE_MOVING_CHILD_F5", "ACTOR_TEMPLATE_TYPE_84_F5_MOVING_CHILD"),
        0x001B846C: ("ACTOR_TEMPLATE_MENU_PRESENTATION_SECONDARY", "ACTOR_TEMPLATE_MENU_TYPE_84_SECONDARY"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases


def test_remaining_interaction_template_roles_preserve_numeric_aliases():
    symbols = SymbolStore()
    expected = {
        0x001B78B4: ("ACTOR_TEMPLATE_INTERACTION_RESPONSE_READY", "ACTOR_TEMPLATE_TYPE_5F_INTERACTION"),
        0x001B78DC: ("ACTOR_TEMPLATE_INTERACTION_THRESHOLD", "ACTOR_TEMPLATE_TYPE_67_INTERACTION"),
        0x001B7A80: ("ACTOR_TEMPLATE_INTERACTION_SELECTION_A", "ACTOR_TEMPLATE_TYPE_05_INTERACTION_VARIANT_A"),
        0x001B7A94: ("ACTOR_TEMPLATE_INTERACTION_SELECTION_B", "ACTOR_TEMPLATE_TYPE_05_INTERACTION_VARIANT_B"),
        0x001B7AA8: ("ACTOR_TEMPLATE_INTERACTION_SELECTION_C", "ACTOR_TEMPLATE_TYPE_05_INTERACTION_VARIANT_C"),
        0x001B7AE4: ("ACTOR_TEMPLATE_INTERACTION_PAIR_VARIANT", "ACTOR_TEMPLATE_TYPE_23_INTERACTION"),
        0x001B7C4C: ("ACTOR_TEMPLATE_INTERACTION_RESPONSE_EXTENDED", "ACTOR_TEMPLATE_TYPE_87_INTERACTION_RESPONSE"),
        0x001B7C60: ("ACTOR_TEMPLATE_INTERACTION_WALL_RESPONSE_CHILD", "ACTOR_TEMPLATE_TYPE_84_TYPE87_RESPONSE_CHILD"),
        0x001B7F08: ("ACTOR_TEMPLATE_MOVING_CHILD_BASE", "ACTOR_TEMPLATE_TYPE_84_F5_MOVING_CHILD_BASE"),
        0x001B7F58: ("ACTOR_TEMPLATE_PRESENTATION_CHILD_SECONDARY", "ACTOR_TEMPLATE_TYPE_30_PRESENTATION_CHILD"),
        0x001B7C9C: ("ACTOR_TEMPLATE_INTERACTION_RESPONSE_BASE", "ACTOR_TEMPLATE_TYPE_84_INTERACTION_RESPONSE"),
        0x001B7D14: ("ACTOR_TEMPLATE_INTERACTION_OFFSET", "ACTOR_TEMPLATE_TYPE_84_INTERACTION_OFFSET"),
        0x001B7DB4: ("ACTOR_TEMPLATE_TERMINAL_INTERACTION_SHARED", "ACTOR_TEMPLATE_TYPE_76_INTERACTION"),
        0x001B7DDC: ("ACTOR_TEMPLATE_TERRAIN_RESPONSE_PRIMARY", "ACTOR_TEMPLATE_TYPE_84_TERRAIN_RESPONSE"),
        0x001B7E54: ("ACTOR_TEMPLATE_BOUNCE_RESPONSE", "ACTOR_TEMPLATE_TYPE_65_BOUNCE"),
        0x001B7EF4: ("ACTOR_TEMPLATE_INTERACTION_Y_OFFSET", "ACTOR_TEMPLATE_TYPE_32_INTERACTION"),
        0x001B7F94: ("ACTOR_TEMPLATE_INTERACTION_FRAME_VARIANT", "ACTOR_TEMPLATE_TYPE_4F_INTERACTION"),
        0x001B7C38: ("ACTOR_TEMPLATE_PROXIMITY_GATE", "ACTOR_TEMPLATE_TYPE_1F"),
        0x001B805C: ("ACTOR_TEMPLATE_TERRAIN_RESPONSE_SCENE5", "ACTOR_TEMPLATE_TERRAIN_TYPE_84"),
        0x001B7E2C: ("ACTOR_TEMPLATE_LANDING_RESPONSE", "ACTOR_TEMPLATE_TYPE_8C"),
        0x001B80E8: ("ACTOR_TEMPLATE_INTERACTION_RESPONSE_VARIANT", "ACTOR_TEMPLATE_TYPE_53_INTERACTION"),
        0x001B80FC: ("ACTOR_TEMPLATE_INTERACTION_VARIANT", "ACTOR_TEMPLATE_TYPE_36_INTERACTION"),
        0x001B8174: ("ACTOR_TEMPLATE_INTERACTION_RESOURCE_VARIANT", "ACTOR_TEMPLATE_TYPE_17_INTERACTION"),
        0x001B81EC: ("ACTOR_TEMPLATE_LEVEL_EVENT_DIRECTIONAL_RESPONSE", "ACTOR_TEMPLATE_TYPE_0C_LEVEL_EVENT"),
        0x001B8228: ("ACTOR_TEMPLATE_LEVEL_EVENT_INTERACTION", "ACTOR_TEMPLATE_TYPE_0B_LEVEL_EVENT"),
        0x001B823C: ("ACTOR_TEMPLATE_LEVEL_EVENT_CHILD", "ACTOR_TEMPLATE_TYPE_2C_LEVEL_EVENT_CHILD"),
        0x001B8250: ("ACTOR_TEMPLATE_INTERACTION_STATE", "ACTOR_TEMPLATE_TYPE_79_INTERACTION"),
        0x001B8390: ("ACTOR_TEMPLATE_LEVEL_EVENT_RESPONSE", "ACTOR_TEMPLATE_TYPE_84_LEVEL_EVENT"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases


def test_decoded_stream_and_palette_roles_preserve_numeric_aliases():
    symbols = SymbolStore()
    expected = {
        0x00121598: ("ACTOR_MOVE_PLAYER_COLLISION_SETTLE_RESPONSE", "ACTOR_MOVE_TYPE62_63_PLAYER_COLLISION_RESPONSE"),
        0x001243B4: ("ACTOR_ANIM_LEVEL_EXIT_CHILD", "ACTOR_ANIM_EXIT_CHILD_TYPE8C"),
        0x001243E2: ("ACTOR_ANIM_LEVEL11_EVENT", "ACTOR_ANIM_TYPE7B_LEVEL11_EVENT"),
        0x001245CC: ("ACTOR_ANIM_PLAYER_BOOTSTRAP_OPENING", "ACTOR_ANIM_TYPE84_F5_OPENING"),
        0x001245E4: ("ACTOR_ANIM_LEVEL08_ENTRY", "ACTOR_ANIM_LEVEL08_TYPE84_ENTRY"),
        0x001250BA: ("ACTOR_ANIM_TERRAIN_SCENE5_RESPONSE_BASE", "ACTOR_ANIM_TYPE84_TERRAIN_SCENE5_BASE"),
        0x001250EA: ("ACTOR_ANIM_PRESENTATION_CHILD_INLINE", "ACTOR_ANIM_TYPE30_PRESENTATION_CHILD_INLINE"),
        0x0012510A: ("ACTOR_ANIM_PRESENTATION_CHILD_SECONDARY", "ACTOR_ANIM_TYPE30_PRESENTATION_CHILD"),
        0x001252F0: ("ACTOR_ANIM_MOVING_INTERACTION", "ACTOR_ANIM_TYPE07_MOVING_INTERACTION"),
        0x001253FC: ("ACTOR_ANIM_LEVEL_EVENT_CHILD_SHARED", "ACTOR_ANIM_TYPE2C_LEVEL_EVENT_CHILD"),
        0x00125874: ("ACTOR_ANIM_LEVEL_ENTRY_OPENING", "ACTOR_ANIM_TYPE42_LEVEL_ENTRY_OPENING"),
        0x00125878: ("ACTOR_ANIM_LEVEL_ENTRY_RESPONSE", "ACTOR_ANIM_TYPE54_LEVEL_ENTRY"),
        0x001258A6: ("ACTOR_ANIM_LEVEL_EVENT_CHILD_OPENING", "ACTOR_ANIM_TYPE84_LEVEL_EVENT_CHILD_OPENING"),
        0x00125940: ("ACTOR_ANIM_LEVEL_ENTRY_VARIANT", "ACTOR_ANIM_TYPE7D_LEVEL_ENTRY"),
        0x001259A6: ("ACTOR_ANIM_DIRECTIONAL_CHILD", "ACTOR_ANIM_TYPE2E_DIRECTIONAL_CHILD"),
        0x00125DC4: ("ACTOR_ANIM_SCENE_INITIAL_PRIMARY", "ACTOR_ANIM_TYPE84_SCENE_INITIAL_PRIMARY"),
        0x00125F5A: ("ACTOR_ANIM_MENU_PRESENTATION_CHILD_PRIMARY", "ACTOR_ANIM_TYPE84_MENU_PRESENTATION_CHILD_A"),
        0x00126042: ("ACTOR_ANIM_LEVEL_EVENT_RESPONSE", "ACTOR_ANIM_TYPE84_LEVEL_EVENT"),
        0x00126056: ("ACTOR_ANIM_LEVEL_EVENT_RESPONSE_CHILD", "ACTOR_ANIM_TYPE84_LEVEL_EVENT_CHILD"),
        0x001261D0: ("ACTOR_ANIM_SCENE_INITIAL_SECONDARY", "ACTOR_ANIM_TYPE84_SCENE_INITIAL_SECONDARY"),
        0x0012620A: ("ACTOR_ANIM_SCENE_RESET", "ACTOR_ANIM_TYPE84_SCENE_RESET"),
        0x00122B6E: ("ACTOR_ANIM_ACTOR_COLLISION_CHILD_RESPONSE", "ACTOR_ANIM_TYPE84_TYPE2D2E31_COLLISION_RESPONSE"),
        0x00123274: ("ACTOR_ANIM_INTERACTION_THRESHOLD", "ACTOR_ANIM_TYPE67_INTERACTION"),
        0x00123E76: ("ACTOR_ANIM_INTERACTION_RESPONSE_SHORT", "ACTOR_ANIM_TYPE84_INTERACTION_RESPONSE"),
        0x00123A7E: ("ACTOR_ANIM_LEVEL12_TERMINAL_EVENT", "ACTOR_ANIM_TYPE2F_LEVEL12_TERMINAL_EVENT"),
        0x00123F10: ("ACTOR_ANIM_INTERACTION_RESPONSE_DELAYED", "ACTOR_ANIM_TYPE37_INTERACTION_RESPONSE"),
        0x00124194: ("ACTOR_ANIM_TERMINAL_INTERACTION_SHARED", "ACTOR_ANIM_TYPE76_INTERACTION"),
        0x00124408: ("ACTOR_ANIM_LANDING_RESPONSE", "ACTOR_ANIM_TYPE8C_LANDING_RESPONSE"),
        0x00124B3A: ("ACTOR_ANIM_INTERACTION_FRAME_RESPONSE_SECONDARY", "ACTOR_ANIM_TYPE4F_INTERACTION"),
        0x00124B6E: ("ACTOR_ANIM_PLAYER_COLLISION_RESPONSE_SHARED", "ACTOR_ANIM_TYPE84_PLAYER_COLLISION_RESPONSE"),
        0x00124BA6: ("ACTOR_ANIM_PLAYER_COLLISION_RESPONSE_ENTRY", "ACTOR_ANIM_TYPE51_PLAYER_COLLISION_RESPONSE"),
        0x00124C18: ("ACTOR_ANIM_ACTOR_COLLISION_CHILD_RESPONSE_ENTRY", "ACTOR_ANIM_TYPE04_COLLISION_RESPONSE"),
        0x001240CE: ("ACTOR_ANIM_WALL_COLLISION_RESPONSE", "ACTOR_ANIM_TYPE_8D_WALL_RESPONSE"),
        0x00124C1A: ("ACTOR_ANIM_SCENE_SETUP_COLLISION_CHILD_SPAWN", "ACTOR_ANIM_TYPE84_MOVING_CHILD_SPAWN_PREFIX"),
        0x00124CDC: ("ACTOR_ANIM_INTERACTION_RESPONSE_SCENE5_ENTRY", "ACTOR_ANIM_TYPE62_SCENE5_RESPONSE_ENTRY"),
        0x00124CE0: ("ACTOR_ANIM_INTERACTION_RESPONSE_DEFAULT_ENTRY", "ACTOR_ANIM_TYPE62_DEFAULT_RESPONSE_ENTRY"),
        0x00124D38: ("ACTOR_ANIM_LEVEL_OBJECT_INTERACTION", "ACTOR_ANIM_TYPE16_LEVEL_OBJECT"),
        0x00124D50: ("ACTOR_ANIM_LEVEL_OBJECT_HORIZONTAL_PROXIMITY_GATE", "ACTOR_ANIM_TYPE16_LEVEL_OBJECT_HORIZONTAL_GATE"),
        0x00124D5C: ("ACTOR_ANIM_LEVEL_OBJECT_COLLISION_RESPONSE_SPAWN_SEQUENCE", "ACTOR_ANIM_TYPE16_COLLISION_RESPONSE_SPAWN_SEQUENCE"),
        0x0012570C: ("ACTOR_ANIM_INTERACTION_EXTENDED_RESPONSE", "ACTOR_ANIM_TYPE36_INTERACTION"),
        0x00125A4C: ("ACTOR_ANIM_INTERACTION_STATE_ROOT", "ACTOR_ANIM_TYPE79_INTERACTION"),
        0x00125D7E: ("ACTOR_ANIM_INTERACTION_RESPONSE_CHILDREN", "ACTOR_ANIM_TYPE41_INTERACTION_RESPONSE"),
        0x00129332: ("LEVEL07_EVENT_PALETTE_SOURCE", "INTERACTION_TYPE7D_PALETTE_SOURCE"),
        0x001294F2: ("INTERACTION_PALETTE_SOURCE_PLAYER_RESPONSE", "INTERACTION_TYPE0C_PALETTE_SOURCE"),
        0x00129532: ("INTERACTION_PALETTE_SOURCE_COLLISION_RESPONSE", "INTERACTION_TYPE0B_PALETTE_SOURCE"),
        0x00129612: ("TERRAIN_RESPONSE_PALETTE_SOURCE", "INTERACTION_TYPE84_TERRAIN_PALETTE_SOURCE"),
        0x001292F2: ("INTERACTION_PRESENTATION_PALETTE_SOURCE", "INTERACTION_TYPE8B_PALETTE_SOURCE"),
    }
    for address, (name, alias) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases


def test_real_type18_19_pair_spawn_gate_has_reset_contract():
    symbol = SymbolStore().at(0x00FFF112, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "PLAYER_COLLISION_PAIR_SPAWN_GATE"
    assert "PLAYER_COLLISION_TYPE18_19_PAIR_SPAWN_GATE" in symbol.aliases
    assert "0x00000276" in symbol.description
    assert "0x00FF0000-0x00FFFFFF" in symbol.description
    assert "no post-set explicit clear" in symbol.description


def test_real_interaction_resource_completion_latches_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FFF0F7: "INTERACTION_RESOURCE_FINALIZATION_GATE",
        0x00FFF0F9: "INTERACTION_RESOURCE_COMPLETION_MARKER",
    }
    for address, name in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["type"] == "u8"
        assert symbol.metadata["format"] == "boolean"
        assert symbol.confidence == "decompiled"

    marker = symbols.at(0x00FFF0F9, include_ranges=False)
    assert marker is not None
    assert "INTERACTION_RESOURCE_COMPLETION_LATCH" in marker.aliases
    assert "0x001B0290" in marker.description
    assert "No direct read or clear" in marker.description


def test_real_fixed_actor_render_and_terrain_fields_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FF7E5E: ("PLAYER_ACTOR_SPRITE_ATTRIBUTE_BASE", "vdp_attribute"),
        0x00FF7E6E: ("PLAYER_ACTOR_SPRITE_VRAM_BASE", "vram_address"),
        0x00FF7EBF: ("ACTOR_SLOT_1_TERRAIN_RESPONSE_BYTE", "terrain_response"),
        0x00FF7EA0: ("ACTOR_SLOT_1_SPRITE_ATTRIBUTE_BASE", "vdp_attribute"),
        0x00FF7EB0: ("ACTOR_SLOT_1_SPRITE_VRAM_BASE", "vram_address"),
        0x00FF7EDE: ("ACTOR_SLOT_2_VERTICAL_MOTION", "motion_delta"),
        0x00FF7F01: ("ACTOR_SLOT_2_TERRAIN_RESPONSE_BYTE", "terrain_response"),
    }
    for address, (name, format_name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == format_name
        assert symbol.confidence == "decompiled"


def test_real_scene_script_refresh_and_menu_latches_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FF7E27: ("VIDEO_REFRESH_RATE_HZ", "integer"),
        0x00FFF005: ("SCENE_SCRIPT_WAIT", "boolean"),
        0x00FFF119: ("MENU_OPTIONS_INITIAL_PRESENTATION_LATCH", "boolean"),
    }
    for address, (name, format_name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == format_name
        assert symbol.confidence == "decompiled"

    refresh = symbols.at(0x00FF7E27, include_ranges=False)
    assert refresh is not None
    assert "STARTUP_FRAME_LIMIT" in refresh.aliases


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


def test_scene_setup_loaders_have_stable_destination_identities():
    expected = {
        0x001B477C: (
            "SceneResource_LoadSceneSetupGraphicsTriple",
            "SceneResource_LoadVRAMTriple12CCD8",
        ),
        0x001B47AC: (
            "SceneResource_LoadSharedGraphicsToE000",
            "SceneResource_LoadE000Resource12D0FA",
        ),
        0x001B47BE: (
            "SceneResource_LoadSceneSetupSecondaryE000Graphics",
            "SceneResource_LoadE000Resource12CE06",
        ),
        0x001B47D0: (
            "SceneResource_LoadCommonC000AndSharedBaseGraphics",
            "SceneResource_LoadC000ResourceAndBase136912",
        ),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_scene_resource_variant_loaders_have_stable_transfer_identities():
    expected = {
        0x001B4896: (
            "SceneResource_LoadCommonBaseAndE000ResourcePairA",
            "SceneResource_LoadVRAMPairWithPalette1298F2",
        ),
        0x001B48C4: (
            "SceneResource_LoadCommonBaseAndState00E000",
            "SceneResource_LoadVRAMPairWithPalette1299D2",
        ),
        0x001B48F2: (
            "SceneResource_LoadSharedBaseAndSharedE000",
            "SceneResource_LoadVRAMPairWithPalette129812",
        ),
        0x001B496C: (
            "SceneResource_LoadCommonBaseForState01",
            "SceneResource_LoadVRAMBaseWithPalette1290B2",
        ),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_scene_resource_c000_loaders_have_stable_state_identities():
    expected = {
        0x001B498A: (
            "SceneResource_LoadState07C000AndPrepareFrame",
            "SceneResource_LoadC000Resource12D870",
        ),
        0x001B49B2: (
            "SceneResource_LoadState04C000AndPrepareFrame",
            "SceneResource_LoadC000Resource12DA04",
        ),
        0x001B4A02: (
            "SceneResource_LoadState0BC000AndPrepareFrame",
            "SceneResource_LoadC000Resource12DF6C",
        ),
        0x001B4A2A: (
            "SceneResource_LoadState01SecondaryC000AndPrepareFrame",
            "SceneResource_LoadC000Resource12E176",
        ),
        0x001B4A52: (
            "SceneResource_LoadState01PrimaryC000AndPrepareFrame",
            "SceneResource_LoadC000Resource12E34A",
        ),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_scene_resource_presentation_wrappers_have_stable_state_identities():
    expected = {
        0x001B4BB8: ("SceneResource_RunState01PrimaryPresentation", "SceneResource_Run12E34AStream1270A8"),
        0x001B4BDC: ("SceneResource_RunState01SecondaryPresentation", "SceneResource_Run12E176Stream127134"),
        0x001B4C02: ("SceneResource_RunState03Presentation", "SceneResource_Run12DD76Stream127207"),
        0x001B4C28: ("SceneResource_RunState00Presentation", "SceneResource_Run12DD76Stream127338"),
        0x001B4C4E: ("SceneResource_RunState04Presentation", "SceneResource_Run12DA04Stream1273E9"),
        0x001B4C74: ("SceneResource_RunState05PrimaryPresentation", "SceneResource_Run12DD76Stream127571"),
        0x001B4C9A: ("SceneResource_RunState05SecondaryPresentation", "SceneResource_Run12DD76Stream1275EE"),
        0x001B4CC0: ("SceneResource_RunState07Presentation", "SceneResource_Run12D870Stream12772D"),
        0x001B4DD2: ("SceneResource_RunState0BPresentation", "SceneResource_Run12DF6CStream12792B"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases

    shared = SymbolStore().at(0x001B49DA, include_ranges=False)
    assert shared is not None
    assert shared.name == "SceneResource_LoadSharedC000AndPrepareFrame"
    assert "SceneResource_LoadC000ResourceAndPrepareFrame" in shared.aliases


def test_scene_resource_blank_wrappers_have_stable_state_identities():
    expected = {
        0x001B4CE6: ("SceneResource_RunState07BlankPresentation", "SceneResource_RunBlankStream1277C5"),
        0x001B4DF8: ("SceneResource_RunState01BlankPresentation", "SceneResource_RunBlankStream1279B7"),
        0x001B4E1E: ("SceneResource_RunState03BlankPresentation", "SceneResource_RunBlankStream127AEE"),
        0x001B4E44: ("SceneResource_RunState04BlankPreludePresentation", "SceneResource_RunBlankStream127B60"),
        0x001B4E6A: ("SceneResource_RunState04BlankPresentationA", "SceneResource_RunBlankStream127BD2"),
        0x001B4E90: ("SceneResource_RunState0BBlankPresentation", "SceneResource_RunBlankStream127C42"),
        0x001B4EB6: ("SceneResource_RunState04BlankPresentationB", "SceneResource_RunBlankStream127CB4"),
        0x001B4EDC: ("SceneResource_RunState09BlankPresentation", "SceneResource_RunBlankStream127D74"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_camera_scroll_callbacks_have_stable_profile_identities():
    expected = {
        0x001B52D6: ("Camera_SelectScrollDeltaProfileFull", "Camera_SetScrollDataCursor693E"),
        0x001B52E2: ("Camera_SelectScrollDeltaProfileReduced", "Camera_SetScrollDataCursor6952"),
        0x001B52EE: ("Camera_SelectScrollDeltaProfileTail", "Camera_SetScrollDataCursor695A"),
    }
    for address, (name, legacy) in expected.items():
        symbol = SymbolStore().at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert legacy in symbol.aliases


def test_actor_type84_collision_transition_has_stable_identity():
    symbol = SymbolStore().at(0x001AC0D2, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "Actor_EnterType84CollisionResponse"
    assert "Actor_EnterType84Animation12319C" in symbol.aliases


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
