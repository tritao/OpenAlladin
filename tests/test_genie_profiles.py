from __future__ import annotations

from pathlib import Path

from genie.profiles import ALADDIN, GameProfile, load_profile
from genie.games.aladdin.data import AladdinSemanticDataClassifier
from genie.symbols import Symbol
from genie import runtime


ROOT = Path(__file__).resolve().parents[1]


def test_aladdin_is_the_default_profile():
    assert load_profile() is ALADDIN
    assert ALADDIN.id == "aladdin"
    assert ALADDIN.platform == "genesis"
    assert ROOT / ALADDIN.default_rom == runtime.ROM_DEFAULT


def test_runtime_aliases_are_profile_backed():
    assert runtime.EXPERIMENTS == ROOT / ALADDIN.experiments_manifest
    assert runtime.EVENTS == ROOT / ALADDIN.events_manifest
    assert runtime.GHIDRA_CONFIG == ROOT / ALADDIN.ghidra_config
    assert runtime.INPUT_FORMAT == ALADDIN.input_format
    assert runtime.INPUT_MAPPING == ALADDIN.input_mapping
    assert runtime.EVENT_FORMAT == ALADDIN.event_format
    assert runtime.SEGMENTS_FORMAT == ALADDIN.segments_format
    assert runtime.RUN_FORMAT == ALADDIN.run_format
    assert (runtime.STATE_FORMAT_V2, runtime.STATE_FORMAT_V3) == ALADDIN.state_formats
    assert tuple(runtime.DEFAULT_PARITY_FIELDS) == ALADDIN.parity_fields
    assert runtime.ATOMIC_STATE_FIELDS == ALADDIN.atomic_state_fields
    assert runtime.ATOMIC_ACTOR_FIELDS == ALADDIN.atomic_actor_fields


def test_aladdin_profile_supplies_semantic_data_provider():
    providers = ALADDIN.semantic_providers()
    assert len(providers) == 1
    assert isinstance(providers[0], AladdinSemanticDataClassifier)


def test_aladdin_data_provider_prioritizes_table_semantics():
    provider = AladdinSemanticDataClassifier()
    assert provider.classify_symbol(Symbol(
        0x40,
        "SCENE_RESOURCE_OBJECT_ANIMATION_TABLE",
        "data",
        metadata={"type": "rom_pointer_table"},
    )) == "pointer-table"
    assert provider.classify_symbol(Symbol(
        0x40,
        "ACTOR_FRAME_PHASE_CHILD_ANIMATION_TABLE",
        "data",
        metadata={"type": "actor_spawn_phase_animation_table"},
    )) == "pointer-table"
    assert provider.classify_symbol(Symbol(
        0x40,
        "INITIAL_SCENE_SCRIPT",
        "data",
        metadata={"type": "scene_script"},
    )) == "scene-table"
    assert provider.classify_symbol(Symbol(
        0x40,
        "LEVEL_TABLE",
        "data",
        metadata={"type": "rom_table"},
    )) == "scene-table"
    assert provider.classify_symbol(Symbol(
        0x40,
        "UNCOMPRESSED_GRAPHICS_BAND_001A8320",
        "data",
        metadata={"type": "graphics_data"},
    )) == "graphics"
    assert provider.classify_symbol(Symbol(
        0x40,
        "PLAYER_COLLISION_HANDLER_TYPE_01",
        "data",
        metadata={"type": "rom_pointer"},
    )) == "pointer-table"


def test_profile_is_immutable():
    assert isinstance(ALADDIN, GameProfile)
    try:
        ALADDIN.id = "other"
    except AttributeError:
        pass
    else:
        raise AssertionError("GameProfile must be immutable")


def test_unknown_profile_has_actionable_error():
    try:
        load_profile("missing")
    except ValueError as error:
        assert "missing" in str(error)
        assert "aladdin" in str(error)
    else:
        raise AssertionError("unknown profiles must be rejected")
