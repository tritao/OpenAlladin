from __future__ import annotations

import json
from pathlib import Path

from genie.cli import build_parser
from genie.ghidra.context import build_context
from genie.ghidra.database import AnalysisDatabase
from genie.ghidra.worklist import function_work_queue, symbol_review_queue
from genie.layout.model import Layout, LayoutRange
from genie.symbols import (
    Symbol,
    SymbolStore,
    edit_symbol,
    is_low_information_name,
    mechanical_name,
    name_for,
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
    assert is_low_information_name("Actor_ApplyTerminalCollisionResponse") is False


def test_symbols_cli_surface_dispatches():
    show = build_parser().parse_args(["symbols", "show", "0x001AC784"])
    find = build_parser().parse_args(["symbols", "find", "AnimationVM", "--kind", "function"])
    stats = build_parser().parse_args(["symbols", "stats", "--json"])
    unknown = build_parser().parse_args(["symbols", "unknown", "--kind", "function", "--limit", "4"])
    semantic = build_parser().parse_args(["symbols", "next", "--kind", "function", "--semantic"])
    review = build_parser().parse_args(["symbols", "review", "--kind", "data", "--limit", "4", "--json"])
    rename = build_parser().parse_args(["symbols", "rename", "0x20", "Scene_Init"])
    describe = build_parser().parse_args(["symbols", "describe", "0x20", "entry point"])
    confidence = build_parser().parse_args(["symbols", "confidence", "0x20", "decompiled"])
    assert show.address == 0x1AC784
    assert find.kind == "function"
    assert stats.json_output is True
    assert unknown.limit == 4
    assert semantic.semantic is True
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
        0x001AE9DA: ("PlayerCollision_DelegateToActorBlock", "ActorType06_0F_PlayerCollisionHandler", "confirmed"),
    }
    for address, (name, alias, confidence) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert alias in symbol.aliases
        assert symbol.confidence == confidence


def test_real_actor_terminal_interaction_has_semantic_name_and_legacy_alias():
    symbol = SymbolStore().at(0x001AC458, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "ActorCollision_HandleTerminalInteraction"
    assert "ActorType0A_ActorCollisionHandler" in symbol.aliases
    assert symbol.confidence == "confirmed"


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
    assert latch.name == "PLAYER_INTERACTION_TYPE4B_LATCH"


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
    assert symbol.name == "LEVEL09_TYPE50_SPAWN_COOLDOWN"
    assert symbol.metadata["type"] == "u8"
    assert symbol.metadata["format"] == "counter"


def test_real_level00_type13_palette_delay_has_canonical_role():
    symbol = SymbolStore().at(0x00FFF124, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "LEVEL00_TYPE13_PALETTE_DELAY"
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
    assert symbol.name == "ACTOR_TYPE42_COLLISION_STEP"
    assert symbol.metadata["type"] == "u8"
    assert symbol.metadata["format"] == "counter"


def test_real_collision_and_presentation_state_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FFF10E: ("PLAYER_INTERACTION_TYPE1A_LATCH", "boolean"),
        0x00FFF10F: ("PLAYER_INTERACTION_TYPE1B_LATCH", "boolean"),
        0x00FFF110: ("PLAYER_INTERACTION_TYPE1C_LATCH", "boolean"),
        0x00FFF123: ("INTERACTION_TYPE12_PRESENTATION_LATCH", "boolean"),
        0x00FFF125: ("ACTOR_TYPE18_19_CONTACT_COUNT", "counter"),
        0x00FFF129: ("PLAYER_INTERACTION_TYPE4A_LATCH", "boolean"),
        0x00FFF12A: ("PLAYER_INTERACTION_TYPE4C_LATCH", "boolean"),
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
        0x00FFF179: ("PLAYER_INTERACTION_TYPE3D_LATCH", "boolean", "decompiled"),
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
        0x00FFEFFD: ("INPUT_EDGE_LATCH", "boolean"),
        0x00FF7274: ("MENU_OPTIONS_SUBPHASE", "counter"),
    }
    for address, (name, format_name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == format_name
        assert symbol.confidence == "decompiled"


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
        0x00FFF112: ("PLAYER_COLLISION_TYPE18_19_PAIR_SPAWN_GATE", "boolean"),
        0x00FFF11A: ("LEVEL04_EVENT_38_PENDING", "boolean"),
        0x00FFF11C: ("PLAYER_COLLISION_TYPE29_SPAWN_GATE", "boolean"),
        0x00FFF11D: ("ACTOR_TYPE11_SURFACE_INTERACTION_BLOCK", "boolean"),
        0x00FFF12C: ("LEVEL04_EVENT_45_PENDING", "boolean"),
    }
    for address, (name, format_name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["format"] == format_name
        assert symbol.confidence == "decompiled"


def test_real_interaction_resource_completion_latches_have_canonical_roles():
    symbols = SymbolStore()
    expected = {
        0x00FFF0F7: "INTERACTION_RESOURCE_FINALIZATION_GATE",
        0x00FFF0F9: "INTERACTION_RESOURCE_COMPLETION_LATCH",
    }
    for address, name in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["type"] == "u8"
        assert symbol.metadata["format"] == "boolean"
        assert symbol.confidence == "decompiled"


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
