from __future__ import annotations

import json
import shutil
from pathlib import Path

from genie.ghidra.database import AnalysisDatabase
from genie.ghidra.validate import validate_database
from genie.layout.classifier import build_layout
from genie.layout.model import Layout, LayoutRange
from genie.layout.validate import validate_layout
from genie.symbols import SymbolStore


FILES = (
    "metadata.json",
    "functions.json",
    "instructions.json",
    "callgraph.json",
    "xrefs.json",
    "memory_reads.json",
    "memory_writes.json",
    "indirect_calls.json",
    "jump_tables.json",
    "address_classes.json",
)


def _write_json(path: Path, value) -> None:
    path.write_text(json.dumps(value), encoding="utf-8")


def _write_empty_symbol_tree(root: Path) -> None:
    symbols = root / "re/symbols"
    symbols.mkdir(parents=True)
    for category in ("functions", "ram", "data"):
        (symbols / f"{category}.yml").write_text("\n", encoding="utf-8")


def _write_database(root: Path, *, rom_size: int = 0x100) -> Path:
    database = root / "full-rom"
    database.mkdir(parents=True)
    documents = {
        "metadata.json": {
            "format": "openaladdin-ghidra-full-rom-v1",
            "rom_size": rom_size,
            "files": list(FILES),
        },
        "functions.json": [
            {"address": "0x10", "name": "First", "start": "0x10", "end": "0x1F"},
        ],
        "instructions.json": [],
        "callgraph.json": {"edges": []},
        "xrefs.json": {"references": []},
        "memory_reads.json": {"references": []},
        "memory_writes.json": {"references": []},
        "indirect_calls.json": {"references": []},
        "jump_tables.json": {"tables": []},
        "address_classes.json": {
            "classes": [{
                "start": "0x18",
                "end": "0x1B",
                "class": "DATA",
                "source": "ghidra.defined_data",
                "name": "DefinedData",
            }],
        },
    }
    for filename, value in documents.items():
        _write_json(database / filename, value)
    return database


def test_layout_build_creates_gap_free_partition_with_evidence_precedence(tmp_path):
    _write_empty_symbol_tree(tmp_path)
    database_root = _write_database(tmp_path)
    layout = build_layout(AnalysisDatabase(database_root), root=tmp_path, include_artifacts=False)

    assert validate_layout(layout) == []
    assert layout.at(0x00).layout_class == "UNKNOWN"
    assert layout.at(0x10).layout_class == "CODE"
    assert layout.at(0x19).layout_class == "OPAQUE_DATA"
    assert layout.at(0x1B).source == "ghidra.defined_data"
    assert sum(item.size for item in layout.ranges) == 0x100


def test_layout_uses_recovered_jump_table_extents(tmp_path):
    _write_empty_symbol_tree(tmp_path)
    database_root = _write_database(tmp_path)
    path = database_root / "jump_tables.json"
    _write_json(path, {
        "tables": [{
            "function_name": "SwitchOwner",
            "load_tables": [{"address": "0x40", "entry_size": 2, "count": 4}],
        }],
    })

    layout = build_layout(AnalysisDatabase(database_root), root=tmp_path, include_artifacts=False)
    item = layout.at(0x45)
    assert item is not None
    assert item.layout_class == "JUMP_TABLE"
    assert item.start == 0x40
    assert item.end == 0x47


def test_layout_preserves_sparse_function_body_ranges(tmp_path):
    _write_empty_symbol_tree(tmp_path)
    database_root = _write_database(tmp_path)
    _write_json(database_root / "functions.json", [{
        "address": "0x10",
        "name": "SparseFunction",
        "start": "0x10",
        "end": "0x2F",
        "ranges": [
            {"start": "0x10", "end": "0x17"},
            {"start": "0x28", "end": "0x2F"},
        ],
    }])

    layout = build_layout(AnalysisDatabase(database_root), root=tmp_path, include_artifacts=False)
    assert layout.at(0x10).layout_class == "CODE"
    assert layout.at(0x20).layout_class == "UNKNOWN"
    assert layout.at(0x28).layout_class == "CODE"


def test_layout_uses_chopper_graphics_manifest(tmp_path):
    _write_empty_symbol_tree(tmp_path)
    database_root = _write_database(tmp_path)
    assets = tmp_path / "build/assets/sprites"
    assets.mkdir(parents=True)
    _write_json(assets / "frames.json", {
        "supported": True,
        "pointer_table": "0x20",
        "tile_sets": {"1x1": {"tile_bytes": 32}},
        "frames": [
            {"index": 0, "address": "0x40", "struct_size": 4,
             "parts": [{"tile_set": "1x1", "tile_address": "0x80", "tile_size": 1}]},
            {"index": 1, "address": "0x44", "struct_size": 4,
             "parts": [{"tile_set": "1x1", "tile_address": "0xA0", "tile_size": 1}]},
        ],
    })

    layout = build_layout(AnalysisDatabase(database_root), root=tmp_path)
    assert layout.at(0x20).layout_class == "POINTER_TABLE"
    assert layout.at(0x40).layout_class == "GRAPHICS"
    assert layout.at(0x40).name == "SPRITE_FRAME_0000"
    assert layout.at(0x80).layout_class == "GRAPHICS"
    assert layout.at(0x80).name == "SPRITE_TILE_DATA"


def test_layout_uses_canonical_padding_ranges(tmp_path):
    _write_empty_symbol_tree(tmp_path)
    (tmp_path / "re/symbols/data.yml").write_text(
        """
0x00000020:
  name: ROM_PADDING_FF_00000020
  type: padding_data
  size: 4

0x00000030:
  name: ROM_PADDING_ZERO_00000030
  type: padding_data
  size: 2
""",
        encoding="utf-8",
    )
    database_root = _write_database(tmp_path)

    layout = build_layout(AnalysisDatabase(database_root), root=tmp_path, include_artifacts=False)
    assert layout.at(0x20).layout_class == "PADDING"
    assert layout.at(0x30).layout_class == "PADDING"


def test_layout_uses_canonical_terrain_collision_profile_range(tmp_path):
    _write_empty_symbol_tree(tmp_path)
    (tmp_path / "re/symbols/data.yml").write_text(
        """
0x00000020:
  name: TERRAIN_TILE_COLLISION_PROFILE_TABLE
  type: terrain_collision_profile_table
  entry_size: 16
  count: 256
  size: 4096
""",
        encoding="utf-8",
    )
    database_root = _write_database(tmp_path, rom_size=0x1100)

    layout = build_layout(AnalysisDatabase(database_root), root=tmp_path, include_artifacts=False)
    item = layout.at(0x101F)
    assert item is not None
    assert item.layout_class == "TERRAIN_DATA"
    assert item.start == 0x20 and item.end == 0x101F


def test_canonical_actor_animation_family_has_exact_non_overlapping_ranges():
    symbols = SymbolStore()
    expected = {
        0x00124A2E: (0x00124A6B, "ACTOR_ANIM_TYPE8B_PRESENTATION_F5"),
        0x00124A6C: (0x00124AA9, "ACTOR_ANIM_TYPE8B_PRESENTATION_F6"),
        0x00124AAA: (0x00124ADF, "ACTOR_ANIM_TYPE8B_PRESENTATION_F7"),
        0x00124AE0: (0x00124B15, "ACTOR_ANIM_TYPE8B_PRESENTATION_F8"),
        0x00124B16: (0x00124B39, "ACTOR_ANIM_TYPE4E_INTERACTION"),
        0x00124B3A: (0x00124B6D, "ACTOR_ANIM_TYPE4F_INTERACTION"),
        0x00124BA6: (0x00124BDB, "ACTOR_ANIM_TYPE51_PLAYER_COLLISION_RESPONSE"),
        0x00124BDC: (0x00124C17, "ACTOR_ANIM_TYPE04_SCENE_SETUP"),
        0x00124C3A: (0x00124C9B, "ACTOR_ANIM_TYPE84_MOVING_CHILD_VARIANT_LOOP"),
        0x00124D50: (0x00124D5B, "ACTOR_ANIM_TYPE16_LEVEL_OBJECT_HORIZONTAL_GATE"),
        0x00124D5C: (0x00124F31, "ACTOR_ANIM_TYPE16_COLLISION_RESPONSE_SPAWN_SEQUENCE"),
        0x00124F32: (0x00124F95, "ACTOR_ANIM_TYPE31_CHILD_SPAWN_PREFIX"),
        0x00124F96: (0x00125027, "ACTOR_ANIM_TYPE31_F5_CHILD_A"),
        0x00125028: (0x001250B9, "ACTOR_ANIM_TYPE31_F5_CHILD_B"),
        0x001250EA: (0x00125109, "ACTOR_ANIM_TYPE30_PRESENTATION_CHILD_INLINE"),
    }
    owners = []
    for address, (end, name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        owners.append((symbol.address, symbol.end))
    assert all(right < next_left for (_, right), (next_left, _) in zip(owners, owners[1:]))

    alias = symbols.at(0x00124B94, include_ranges=False)
    assert alias is not None
    assert alias.name == "ACTOR_ANIM_TYPE50_LEVEL09_RESPONSE_ENTRY"
    assert alias.metadata["alias_of"] == "ACTOR_ANIM_TYPE84_PLAYER_COLLISION_RESPONSE"


def test_type84_interaction_base_b6_animation_range_is_exact():
    symbols = SymbolStore()
    handler = symbols.at(0x001B70F8, include_ranges=False)
    assert handler is not None
    assert handler.name == "InteractionSpawn_Type84Base_B6"

    symbol = symbols.at(0x001242B0, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "ACTOR_ANIM_TYPE84_BASE_B6"
    assert symbol.end == 0x001242C9
    assert symbol.size == 26
    assert symbol.metadata["type"] == "animation_stream"

    following = symbols.at(0x001242CA, include_ranges=False)
    assert following is not None
    assert following.name == "ACTOR_ANIM_TYPE84_BASE_B7"
    assert symbol.end + 1 == following.address


def test_type0f_child_and_type6e_default_animation_ranges_are_exact():
    symbols = SymbolStore()
    expected = {
        0x00123D34: (0x00123DE1, "ACTOR_ANIM_TYPE84_TYPE0F_CHILD"),
        0x00123DEA: (0x00123E35, "ACTOR_ANIM_TYPE6E_73_BASE_DEFAULT"),
        0x00123E36: (0x00123E75, "ACTOR_ANIM_TYPE84_RUNTIME47_4C"),
        0x00123E76: (0x00123E7D, "ACTOR_ANIM_TYPE84_INTERACTION_RESPONSE"),
    }
    owners = []
    for address, (end, name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        owners.append((symbol.address, symbol.end))
    assert all(right < next_left for (_, right), (next_left, _) in zip(owners, owners[1:]))

    child = symbols.at(0x00123D34, include_ranges=False)
    assert child is not None
    assert child.size == 174
    assert symbols.at(0x00123DE2, include_ranges=False) is None


def test_mid_actor_animation_stream_ranges_are_exact():
    symbols = SymbolStore()
    expected = {
        0x00122C40: (0x00122C65, "ACTOR_ANIM_TYPE44_INTERACTION"),
        0x00122C66: (0x00122CAB, "ACTOR_ANIM_TYPE46_SHARED_SPAWN"),
        0x00122D54: (0x00122D91, "ACTOR_ANIM_TYPE55_INTERACTION"),
        0x00122DB2: (0x00122DD7, "ACTOR_ANIM_TYPE84_TYPE01_RESPONSE"),
        0x00122DD8: (0x00122DED, "ACTOR_ANIM_TYPE84_TYPE2D_INTERACTION_RESPONSE"),
        0x00122DEE: (0x00122DF1, "ACTOR_ANIM_TYPE84_MENU_PRESENTATION"),
        0x00122DF2: (0x00122E15, "ACTOR_ANIM_TYPE03_INTERACTION"),
    }
    owners = []
    for address, (end, name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        owners.append((symbol.address, symbol.end))
    assert all(right < next_left for (_, right), (next_left, _) in zip(owners, owners[1:]))


def test_type03_collision_response_animation_family_is_exact():
    symbols = SymbolStore()
    expected = {
        0x00122E16: (0x00122EDF, "ACTOR_ANIM_TYPE84_TYPE03_COLLISION_RESPONSE"),
        0x00122EE0: (0x00122EFD, "ACTOR_ANIM_TYPE84_F5_MOVING_CHILD_SPAWN_ENTRY"),
        0x00122EFE: (0x00122F37, "ACTOR_ANIM_TYPE84_F5_MOVING_CHILD_RESPONSE"),
    }
    owners = []
    for address, (end, name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        owners.append((symbol.address, symbol.end))
    assert all(right < next_left for (_, right), (next_left, _) in zip(owners, owners[1:]))

    root = symbols.at(0x00122E96, include_ranges=False)
    assert root is not None
    assert root.name == "ACTOR_ANIM_TYPE84_F5_MOVING_CHILD_ROOT"
    assert root.metadata["alias_of"] == "ACTOR_ANIM_TYPE84_TYPE03_COLLISION_RESPONSE"
    assert root.metadata["entry_offset"] == 128

    loop = symbols.at(0x00122F06, include_ranges=False)
    assert loop is not None
    assert loop.name == "ACTOR_ANIM_TYPE84_F5_MOVING_CHILD_RESPONSE_LOOP"
    assert loop.metadata["alias_of"] == "ACTOR_ANIM_TYPE84_F5_MOVING_CHILD_RESPONSE"
    assert loop.metadata["entry_offset"] == 8


def test_shared_type84_0f22_response_range_is_exact():
    symbols = SymbolStore()
    symbol = symbols.at(0x00122F80, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "ACTOR_ANIM_TYPE84_SHARED_0F22_RESPONSE"
    assert symbol.end == 0x00122FA1
    assert symbol.size == 34

    following = symbols.at(0x00122FA2, include_ranges=False)
    assert following is not None
    assert following.name == "ACTOR_ANIM_DEATH_122FA2"
    assert symbol.end < following.address


def test_type87_interaction_response_animation_family_is_exact():
    symbols = SymbolStore()
    expected = {
        0x00123AC4: (0x00123AFF, "ACTOR_ANIM_TYPE87_SHARED_PRESENTATION_PHASE_A"),
        0x00123B00: (0x00123B37, "ACTOR_ANIM_TYPE87_SHARED_PRESENTATION_PHASE_B"),
        0x00123B38: (0x00123C83, "ACTOR_ANIM_TYPE87_INTERACTION_RESPONSE"),
        0x00123C84: (0x00123CF7, "ACTOR_ANIM_TYPE84_TYPE87_RESPONSE_CHILD"),
    }
    owners = []
    for address, (end, name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        owners.append((symbol.address, symbol.end))
    assert all(right < next_left for (_, right), (next_left, _) in zip(owners, owners[1:]))

    collision = symbols.at(0x00123B66, include_ranges=False)
    assert collision is not None
    assert collision.name == "ACTOR_ANIM_TYPE87_COLLISION_RESPONSE_ENTRY"
    assert collision.metadata["alias_of"] == "ACTOR_ANIM_TYPE87_INTERACTION_RESPONSE"
    assert collision.metadata["entry_offset"] == 46

    loop = symbols.at(0x00123C6A, include_ranges=False)
    assert loop is not None
    assert loop.name == "ACTOR_ANIM_TYPE87_INTERACTION_RESPONSE_LOOP"
    assert loop.metadata["alias_of"] == "ACTOR_ANIM_TYPE87_INTERACTION_RESPONSE"
    assert loop.metadata["entry_offset"] == 306


def test_type37_interaction_response_animation_range_is_exact():
    symbols = SymbolStore()
    symbol = symbols.at(0x00123F10, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "ACTOR_ANIM_TYPE37_INTERACTION_RESPONSE"
    assert symbol.end == 0x00123F7D
    assert symbol.size == 110

    following = symbols.at(0x00123F7E, include_ranges=False)
    assert following is not None
    assert following.name == "ACTOR_ANIM_TYPE84_SCENE_RESOURCE_RESPONSE_PREFIX"
    assert symbol.end < following.address


def test_upper_collision_response_animation_family_is_exact():
    symbols = SymbolStore()
    expected = {
        0x001233CC: (0x0012340B, "ACTOR_ANIM_TYPE20_COLLISION_RESPONSE"),
        0x0012340C: (0x001234BD, "ACTOR_ANIM_TYPE1D_INTERACTION_RESPONSE"),
        0x001234BE: (0x001234F5, "ACTOR_ANIM_TYPE1E_ACTOR_COLLISION_RESPONSE"),
        0x001234F6: (0x0012350B, "ACTOR_ANIM_TYPE1E_ACTOR_COLLISION_RESPONSE_ALTERNATE"),
        0x0012350C: (0x00123543, "ACTOR_ANIM_TYPE21_ACTOR_COLLISION_RESPONSE"),
        0x00123544: (0x00123559, "ACTOR_ANIM_TYPE21_ACTOR_COLLISION_RESPONSE_ALTERNATE"),
    }
    owners = []
    for address, (end, name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        assert symbol.size == end - address + 1
        assert symbol.metadata["type"] == "animation_stream"
        owners.append((symbol.address, symbol.end))
    assert all(right + 1 == next_left for (_, right), (next_left, _) in zip(owners, owners[1:]))

    aliases = {
        0x0012341E: ("ACTOR_ANIM_TYPE1D_Y_PROXIMITY_ENTRY", 18),
        0x0012342E: ("ACTOR_ANIM_TYPE1D_STATE_GATE", 34),
        0x00123450: ("ACTOR_ANIM_TYPE1D_MOVEMENT_RESPONSE", 68),
        0x00123458: ("ACTOR_ANIM_TYPE1D_MOVEMENT_LOOP", 76),
        0x00123490: ("ACTOR_ANIM_TYPE1D_GUARD_SPAWN", 132),
    }
    for address, (name, offset) in aliases.items():
        alias = symbols.at(address, include_ranges=False)
        assert alias is not None
        assert alias.name == name
        assert alias.metadata["alias_of"] == "ACTOR_ANIM_TYPE1D_INTERACTION_RESPONSE"
        assert alias.metadata["entry_offset"] == offset


def test_layout_treats_rnc_manifest_end_as_exclusive(tmp_path):
    _write_empty_symbol_tree(tmp_path)
    database_root = _write_database(tmp_path)
    assets = tmp_path / "build/assets"
    assets.mkdir(parents=True)
    _write_json(assets / "manifest.json", {
        "rom": {"size": 0x100},
        "inventory": {"rnc_blocks": [
            {"offset": "0x20", "end": "0x28", "references": ["first"]},
            {"offset": "0x28", "end": "0x30", "references": ["second"]},
        ]},
    })

    layout = build_layout(AnalysisDatabase(database_root), root=tmp_path)
    first = layout.at(0x27)
    second = layout.at(0x28)
    assert first is not None and first.layout_class == "COMPRESSED_DATA"
    assert first.end == 0x27
    assert second is not None and second.layout_class == "COMPRESSED_DATA"
    assert second.start == 0x28


def test_layout_uses_level_block_dictionary_extents(tmp_path):
    _write_empty_symbol_tree(tmp_path)
    database_root = _write_database(tmp_path)
    assets = tmp_path / "build/assets"
    assets.mkdir(parents=True)
    _write_json(assets / "levels.json", {
        "levels": [
            {"index": 0, "assets": {"block_dictionary": {"rom": "0x20", "bytes": 8}}},
            {"index": 1, "assets": {"block_dictionary": {"rom": "0x20", "bytes": 16}}},
            {"index": 2, "assets": {"block_dictionary": {"rom": "0x30", "bytes": 8}}},
        ],
    })

    layout = build_layout(AnalysisDatabase(database_root), root=tmp_path)
    shared = layout.at(0x2F)
    later = layout.at(0x30)
    assert shared is not None and shared.layout_class == "LEVEL_DATA"
    assert shared.start == 0x20 and shared.end == 0x2F
    assert later is not None and later.layout_class == "LEVEL_DATA"
    assert later.name == "LEVEL_BLOCK_DICTIONARY_000030"


def test_layout_uses_scene_resource_stream_extents(tmp_path):
    _write_empty_symbol_tree(tmp_path)
    database_root = _write_database(tmp_path)
    assets = tmp_path / "build/assets"
    assets.mkdir(parents=True)
    _write_json(assets / "scene_transitions.json", {
        "rom_size": 0x100,
        "streams": [{
            "name": "SCENE_TRANSITION_RESOURCE_STREAM_STATE_00",
            "entry": "0x40",
            "bytes_decoded": 0x10,
        }],
    })

    layout = build_layout(AnalysisDatabase(database_root), root=tmp_path)
    stream = layout.at(0x4F)
    assert stream is not None
    assert stream.layout_class == "SCENE_TABLE"
    assert stream.start == 0x40 and stream.end == 0x4F
    assert stream.name == "SCENE_TRANSITION_RESOURCE_STREAM_STATE_00"


def test_layout_classifies_sound_test_entry_table_as_scene_data(tmp_path):
    _write_empty_symbol_tree(tmp_path)
    (tmp_path / "re/symbols/data.yml").write_text(
        """
0x00000020:
  name: SOUND_TEST_ENTRY_TABLE
  type: sound_test_entry_table
  size: 16
""",
        encoding="utf-8",
    )
    database_root = _write_database(tmp_path)

    layout = build_layout(AnalysisDatabase(database_root), root=tmp_path, include_artifacts=False)
    table = layout.at(0x20)
    assert table is not None
    assert table.layout_class == "SCENE_TABLE"


def test_canonical_scene_resource_presentation_stream_ranges_are_exact_and_adjacent():
    symbols = SymbolStore()
    expected = (
        (0x00127834, 0x0012792A, "SCENE_RESOURCE_PRESENTATION_STREAM_STATE_10"),
        (0x0012792B, 0x001279B6, "SCENE_RESOURCE_PRESENTATION_STREAM_STATE_0B"),
        (0x001279B7, 0x00127AED, "SCENE_RESOURCE_BLANK_STREAM_STATE_01"),
        (0x00127AEE, 0x00127BD1, "SCENE_RESOURCE_BLANK_STREAM_STATE_03"),
        (0x00127BD2, 0x00127C41, "SCENE_RESOURCE_BLANK_STREAM_STATE_04_A"),
        (0x00127C42, 0x00127CB3, "SCENE_RESOURCE_BLANK_STREAM_STATE_0B"),
        (0x00127CB4, 0x00127D73, "SCENE_RESOURCE_BLANK_STREAM_STATE_04_B"),
        (0x00127D74, 0x00127E7F, "SCENE_RESOURCE_BLANK_STREAM_STATE_09"),
    )
    actual = []
    for address, end, name in expected:
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == end - address + 1
        assert symbol.end == end
        assert symbol.metadata["type"] == "scene_resource_stream"
        actual.append((symbol.address, symbol.end))
    assert actual == [(start, end) for start, end, _ in expected]
    assert all(left[1] + 1 == right[0] for left, right in zip(actual, actual[1:]))


def test_canonical_scene_transition_presentation_stream_has_exact_terminal():
    symbols = SymbolStore()
    symbol = symbols.at(0x0012622E, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "SCENE_TRANSITION_PRESENTATION_STREAM_12622E"
    assert symbol.size == 0x2E4
    assert symbol.end == 0x00126511
    assert symbol.metadata["type"] == "scene_resource_stream"


def test_canonical_scene_transition_menu_streams_have_exact_terminals():
    symbols = SymbolStore()
    expected = (
        (0x00126512, 0x00126515, "SCENE_TRANSITION_LABEL_ON"),
        (0x00126516, 0x00126519, "SCENE_TRANSITION_LABEL_OFF"),
        (0x0012651A, 0x0012652C, "ROM_SCENE_TRANSITION_SELECTION_STREAM"),
        (0x0012652D, 0x0012654E, "SCENE_TRANSITION_SELECTION_ROW_STATUS_32"),
        (0x0012654F, 0x0012656F, "SCENE_TRANSITION_SELECTION_ROW_DEFAULT"),
        (0x00126570, 0x0012659B, "MENU_WISH_PROMPT_STREAM"),
        (0x00126679, 0x00126691, "MENU_OPTIONS_HEADER_STREAM"),
        (0x00126692, 0x0012669F, "MENU_DIFFICULTY_PROMPT_STREAM"),
        (0x001266A0, 0x0012671D, "MENU_OPTIONS_PRESENTATION_STREAM"),
    )
    actual = []
    for start, end, name in expected:
        symbol = symbols.at(start, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == end - start + 1
        assert symbol.end == end
        assert symbol.metadata["type"] == "scene_resource_stream"
        actual.append((symbol.address, symbol.end))
    assert actual == [(start, end) for start, end, _ in expected]


def test_canonical_fixed_palette_sources_have_exact_upload_extents():
    symbols = SymbolStore()
    expected = (
        (0x00129012, 0x00129031, "MENU_PALETTE_BAND_SOURCE_129012"),
        (0x00129092, 0x001290B1, "LEVEL00_PALETTE_BAND_SOURCE"),
        (0x001290B2, 0x001290D1, "SCENE_RESOURCE_PALETTE_BAND_SOURCE"),
        (0x00129152, 0x00129171, "INTERACTION_TYPE13_PALETTE_SOURCE"),
        (0x001292B2, 0x001292D1, "INTERACTION_PALETTE_SOURCE_1292B2"),
        (0x001292F2, 0x00129311, "INTERACTION_TYPE8B_PALETTE_SOURCE"),
        (0x00129332, 0x00129351, "INTERACTION_TYPE7D_PALETTE_SOURCE"),
        (0x001294F2, 0x00129511, "INTERACTION_TYPE0C_PALETTE_SOURCE"),
        (0x00129532, 0x00129551, "INTERACTION_TYPE0B_PALETTE_SOURCE"),
        (0x001295D2, 0x001295F1, "INTERACTION_TYPE12_PALETTE_SOURCE"),
        (0x001295F2, 0x00129611, "INTERACTION_TYPE11_PALETTE_SOURCE"),
        (0x00129612, 0x00129631, "INTERACTION_TYPE84_TERRAIN_PALETTE_SOURCE"),
        (0x00129B52, 0x00129B71, "SCENE_TRANSITION_PALETTE_BAND_SOURCE"),
        (0x00129B72, 0x00129B91, "SCENE_TRANSITION_READY_PALETTE_SOURCE"),
    )
    for start, end, name in expected:
        symbol = symbols.at(start, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == 0x20
        assert symbol.end == end
        assert symbol.metadata["type"] == "palette_data"


def test_canonical_scene_transition_graphics_have_exact_service_extents():
    symbols = SymbolStore()
    expected = (
        (0x00129B92, 0x00129BD1, "ROM_SCENE_TRANSITION_TILE_BAND", "graphics_data"),
        (0x00129BD2, 0x00129C51, "SCENE_TERMINAL_TRANSITION_PALETTE_SOURCE", "palette_data"),
        (0x00129C52, 0x00129C89, "ROM_TERMINAL_TRANSITION_TILE_BAND", "graphics_data"),
        (0x00129C8A, 0x00129CA9, "SCENE_TERMINAL_TRANSITION_PALETTE_BAND0_SOURCE", "palette_data"),
    )
    actual = []
    for start, end, name, symbol_type in expected:
        symbol = symbols.at(start, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == end - start + 1
        assert symbol.end == end
        assert symbol.metadata["type"] == symbol_type
        actual.append((symbol.address, symbol.end))
    assert actual == [(start, end) for start, end, _, _ in expected]
    assert all(left[1] + 1 == right[0] for left, right in zip(actual, actual[1:]))


def test_canonical_title_menu_graphics_have_exact_service_extents():
    symbols = SymbolStore()
    expected = (
        (0x00129CEA, 0x00129D69, "TITLE_TRANSITION_PALETTE_SOURCE", "palette_data"),
        (0x00129D6A, 0x00129D89, "SCENE_PRESENTATION_PALETTE_BAND0_SOURCE", "palette_data"),
        (0x00129D8A, 0x00129DA9, "MENU_WISH_PROMPT_PALETTE_BAND0_SOURCE", "palette_data"),
        (0x00129DAA, 0x00129DFF, "MENU_SELECTION_MARKER_TILE_TABLE", "graphics_data"),
    )
    actual = []
    for start, end, name, symbol_type in expected:
        symbol = symbols.at(start, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == end - start + 1
        assert symbol.end == end
        assert symbol.metadata["type"] == symbol_type
        actual.append((symbol.address, symbol.end))
    assert actual == [(start, end) for start, end, _, _ in expected]
    assert all(left[1] + 1 == right[0] for left, right in zip(actual, actual[1:]))


def test_canonical_scene_resource_palette_sources_have_exact_loader_extents():
    symbols = SymbolStore()
    expected = (
        (0x001297F2, 0x00129811, "MENU_PALETTE_BAND1_SOURCE"),
        (0x00129812, 0x00129831, "SCENE_RESOURCE_PALETTE_BAND2_SOURCE_129812"),
        (0x00129832, 0x001298B1, "SCENE_REBUILD_TRANSITION_PALETTE_SOURCE"),
        (0x001298B2, 0x001298D1, "SCENE_RESOURCE_PALETTE_BAND3_SOURCE"),
        (0x001298D2, 0x001298F1, "SCENE_RESOURCE_PALETTE_BAND2_SOURCE_1298D2"),
        (0x001298F2, 0x00129911, "SCENE_RESOURCE_PALETTE_BAND2_SOURCE_1298F2"),
        (0x00129912, 0x00129931, "SCENE_RESOURCE_PALETTE_BAND0_SOURCE_129912"),
        (0x00129932, 0x00129951, "SCENE_RESOURCE_PALETTE_BAND0_SOURCE_129932"),
        (0x00129952, 0x00129971, "SCENE_RESOURCE_PALETTE_BAND0_SOURCE_129952"),
        (0x00129972, 0x00129991, "SCENE_RESOURCE_PALETTE_BAND0_SOURCE_129972"),
        (0x00129992, 0x001299B1, "SCENE_RESOURCE_PALETTE_BAND0_SOURCE_129992"),
        (0x001299B2, 0x001299D1, "SCENE_RESOURCE_PALETTE_BAND0_SOURCE_1299B2"),
        (0x001299D2, 0x001299F1, "SCENE_RESOURCE_PALETTE_BAND2_SOURCE_1299D2"),
        (0x001299F2, 0x00129A11, "SCENE_RESET_PALETTE_BAND1_SOURCE"),
        (0x00129A12, 0x00129A31, "SCENE_RESOURCE_PALETTE_BAND2_SOURCE_129A12"),
        (0x00129A92, 0x00129AB1, "SCENE_RESOURCE_PALETTE_BAND1_SOURCE_129A92"),
        (0x00129AB2, 0x00129AD1, "SCENE_RESOURCE_PALETTE_BAND1_SOURCE_129AB2"),
    )
    for start, end, name in expected:
        symbol = symbols.at(start, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == end - start + 1
        assert symbol.end == end
        assert symbol.metadata["type"] == "palette_data"


def test_canonical_scene_reset_credits_palette_sources_have_exact_extents():
    symbols = SymbolStore()
    expected = (
        (0x00129E00, 0x00129E7F, "SCENE_RESET_TRANSITION_PALETTE_SOURCE"),
        (0x00129E80, 0x00129EFF, "CREDITS_TRANSITION_PALETTE_SOURCE"),
    )
    actual = []
    for start, end, name in expected:
        symbol = symbols.at(start, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == 0x80
        assert symbol.end == end
        assert symbol.metadata["type"] == "palette_data"
        actual.append((symbol.address, symbol.end))
    assert actual == [(start, end) for start, end, _ in expected]
    assert actual[0][1] + 1 == actual[1][0]


def test_canonical_level_palette_sources_have_exact_transition_extents():
    symbols = SymbolStore()
    expected = (
        (0x00129052, 0x001290D1, "LEVEL_PALETTE_SOURCE_STATE_00_02"),
        (0x001290D2, 0x00129151, "LEVEL_PALETTE_SOURCE_STATE_01"),
        (0x00129172, 0x001291F1, "LEVEL_PALETTE_SOURCE_STATE_03"),
        (0x001291F2, 0x00129271, "LEVEL_PALETTE_SOURCE_STATE_04"),
        (0x00129272, 0x001292F1, "LEVEL_PALETTE_SOURCE_STATE_05_06"),
        (0x00129332, 0x001293B1, "INTERACTION_TYPE7D_PALETTE_SOURCE"),
        (0x001293B2, 0x00129431, "LEVEL_PALETTE_SOURCE_STATE_08"),
        (0x00129432, 0x001294B1, "LEVEL_PALETTE_SOURCE_STATE_09"),
        (0x001294B2, 0x00129531, "LEVEL_PALETTE_SOURCE_STATE_10"),
        (0x00129552, 0x001295D1, "LEVEL_PALETTE_SOURCE_STATE_11"),
        (0x00129632, 0x001296B1, "LEVEL_PALETTE_SOURCE_STATE_12"),
    )
    for start, end, name in expected:
        symbol = symbols.at(start, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == 0x80 if start != 0x00129332 else symbol.size == 0x20
        assert symbol.end == (end if start != 0x00129332 else 0x00129351)
        assert symbol.metadata["type"] == "palette_data"
    remainder = symbols.at(0x00129352, include_ranges=False)
    assert remainder is not None
    assert remainder.name == "LEVEL_PALETTE_SOURCE_STATE_07_REMAINDER"
    assert remainder.size == 0x60
    assert remainder.end == 0x001293B1
    assert remainder.metadata["type"] == "palette_data"
    assert "LEVEL_PALETTE_SOURCE_STATE_07" in symbols.at(0x00129332, include_ranges=False).aliases


def test_canonical_credits_stream_has_exact_interpreter_terminal():
    symbols = SymbolStore()
    symbol = symbols.at(0x00127E8C, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "ROM_CREDITS_STREAM"
    assert symbol.size == 0xFB9
    assert symbol.end == 0x00128E44
    assert symbol.metadata["type"] == "scene_resource_stream"


def test_canonical_sound_test_entry_table_has_complete_sentinel_record():
    symbols = SymbolStore()
    symbol = symbols.at(0x0012675E, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "SOUND_TEST_ENTRY_TABLE"
    assert symbol.size == 0x5F0
    assert symbol.end == 0x00126D4D
    assert symbol.metadata["type"] == "sound_test_entry_table"
    assert symbol.metadata["entry_size"] == 0x10
    assert symbol.metadata["count"] == 95
    assert symbol.metadata["active_count"] == 94


def test_canonical_scene_resource_mode_record_table_has_exact_records_and_boundary():
    symbols = SymbolStore()
    symbol = symbols.at(0x00126D7E, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "SCENE_RESOURCE_MODE_RECORD_TABLE"
    assert symbol.size == 0x138
    assert symbol.end == 0x00126EB5
    assert symbol.metadata["type"] == "scene_resource_record_table"
    assert symbol.metadata["entry_size"] == 0x0C
    assert symbol.metadata["count"] == 26


def test_canonical_scene_resource_mode_streams_are_exact_and_contiguous():
    symbols = SymbolStore()
    expected = (
        (0x00126F0E, 0x00126F1E, "SCENE_RESOURCE_MODE_STREAM_00"),
        (0x00126F1F, 0x00126F2D, "SCENE_RESOURCE_MODE_STREAM_01"),
        (0x00126F2E, 0x00126F3C, "SCENE_RESOURCE_MODE_STREAM_02"),
        (0x00126F3D, 0x00126F47, "SCENE_RESOURCE_MODE_STREAM_03"),
        (0x00126F48, 0x00126F58, "SCENE_RESOURCE_MODE_STREAM_04"),
        (0x00126F59, 0x00126F68, "SCENE_RESOURCE_MODE_STREAM_05"),
        (0x00126F69, 0x00126F78, "SCENE_RESOURCE_MODE_STREAM_06"),
        (0x00126F79, 0x00126F83, "SCENE_RESOURCE_MODE_STREAM_07"),
        (0x00126F84, 0x00126F8C, "SCENE_RESOURCE_MODE_STREAM_08"),
        (0x00126F8D, 0x00126F9C, "SCENE_RESOURCE_MODE_STREAM_09"),
        (0x00126F9D, 0x00126FAC, "SCENE_RESOURCE_MODE_STREAM_0A"),
        (0x00126FAD, 0x00126FBB, "SCENE_RESOURCE_MODE_STREAM_0B"),
        (0x00126FBC, 0x00126FCC, "SCENE_RESOURCE_MODE_STREAM_0C"),
        (0x00126FCD, 0x00126FDF, "SCENE_RESOURCE_MODE_STREAM_0D"),
        (0x00126FE0, 0x00126FE9, "SCENE_RESOURCE_MODE_STREAM_0E"),
        (0x00126FEA, 0x00126FFB, "SCENE_RESOURCE_MODE_STREAM_0F"),
        (0x00126FFC, 0x0012700F, "SCENE_RESOURCE_MODE_STREAM_10"),
        (0x00127010, 0x00127022, "SCENE_RESOURCE_MODE_STREAM_11"),
        (0x00127023, 0x00127032, "SCENE_RESOURCE_MODE_STREAM_14"),
        (0x00127033, 0x00127044, "SCENE_RESOURCE_MODE_STREAM_15"),
        (0x00127045, 0x00127054, "SCENE_RESOURCE_MODE_STREAM_12"),
        (0x00127055, 0x0012705F, "SCENE_RESOURCE_MODE_STREAM_13"),
        (0x00127060, 0x0012706D, "SCENE_RESOURCE_MODE_STREAM_16"),
        (0x0012706E, 0x00127082, "SCENE_RESOURCE_MODE_STREAM_17"),
        (0x00127083, 0x00127092, "SCENE_RESOURCE_MODE_STREAM_18"),
        (0x00127093, 0x001270A6, "SCENE_RESOURCE_MODE_STREAM_19"),
    )
    actual = []
    for start, end, name in expected:
        symbol = symbols.at(start, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == end - start + 1
        assert symbol.end == end
        assert symbol.metadata["type"] == "scene_resource_stream"
        actual.append((symbol.address, symbol.end))
    assert actual == [(start, end) for start, end, _ in expected]
    assert all(left[1] + 1 == right[0] for left, right in zip(actual, actual[1:]))


def test_canonical_scene_resource_presentation_streams_have_exact_terminals():
    symbols = SymbolStore()
    expected = (
        (0x001270A8, 0x00127133, "SCENE_RESOURCE_PRESENTATION_STREAM_12E34A_1270A8"),
        (0x00127134, 0x00127206, "SCENE_RESOURCE_PRESENTATION_STREAM_12E176_127134"),
        (0x00127207, 0x00127337, "SCENE_RESOURCE_PRESENTATION_STREAM_12DD76_127207"),
        (0x00127338, 0x001273E8, "SCENE_RESOURCE_PRESENTATION_STREAM_12DD76_127338"),
        (0x001273E9, 0x001274EF, "SCENE_RESOURCE_PRESENTATION_STREAM_12DA04_1273E9"),
        (0x00127571, 0x001275ED, "SCENE_RESOURCE_PRESENTATION_STREAM_12DD76_127571"),
        (0x001275EE, 0x0012772C, "SCENE_RESOURCE_PRESENTATION_STREAM_12DD76_1275EE"),
        (0x0012772D, 0x001277C4, "SCENE_RESOURCE_PRESENTATION_STREAM_12D870_12772D"),
    )
    for start, end, name in expected:
        symbol = symbols.at(start, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == end - start + 1
        assert symbol.end == end
        assert symbol.metadata["type"] == "scene_resource_stream"


def test_canonical_scene_palette_banks_have_exact_transition_source_extents():
    symbols = SymbolStore()
    expected = (
        (0x00128ED2, 0x00128F51, "SCENE_BLANK_PALETTE"),
        (0x00128FD2, 0x00129051, "SCENE_TRANSITION_PALETTE_SOURCE"),
    )
    for start, end, name in expected:
        symbol = symbols.at(start, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == end - start + 1
        assert symbol.end == end
        assert symbol.metadata["type"] == "palette_data"


def test_layout_does_not_split_owner_for_embedded_symbol_alias(tmp_path):
    _write_empty_symbol_tree(tmp_path)
    (tmp_path / "re/symbols/data.yml").write_text(
        """
0x00000010:
  name: OwnerStream
  size: 16
  type: animation_stream

0x00000014:
  name: EmbeddedEntry
  alias_of: OwnerStream
  entry_offset: 4
  type: animation_stream
""",
        encoding="utf-8",
    )
    database_root = _write_database(tmp_path)

    layout = build_layout(AnalysisDatabase(database_root), root=tmp_path, include_artifacts=False)
    item = layout.at(0x14)
    assert item is not None
    assert item.name == "OwnerStream"
    assert item.start == 0x10
    assert item.end == 0x1F


def test_layout_validator_rejects_overlap_and_gap():
    layout = Layout(
        rom_size=0x10,
        ranges=(
            LayoutRange(0x00, 0x08, "UNKNOWN", "test"),
            LayoutRange(0x08, 0x0F, "CODE", "test"),
        ),
    )
    errors = validate_layout(layout)
    assert any("overlap" in error for error in errors)


def _write_validated_database_fixture(tmp_path: Path) -> Path:
    from genie.common import parse_int
    from genie.symbols import SymbolStore

    scheduler_source = Path(__file__).parents[1] / "re/scheduler/frame_phases.yml"
    scheduler_target = tmp_path / "re/scheduler/frame_phases.yml"
    scheduler_target.parent.mkdir(parents=True)
    shutil.copyfile(scheduler_source, scheduler_target)
    source_root = Path(__file__).parents[1]
    for category in ("functions", "ram", "data"):
        target = tmp_path / "re/symbols" / f"{category}.yml"
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source_root / "re/symbols" / f"{category}.yml", target)

    symbols = SymbolStore(root=tmp_path)
    functions = [
        {
            "address": f"0x{symbol.address:08X}",
            "name": symbol.name,
            "start": f"0x{symbol.address:08X}",
            "end": f"0x{symbol.address:08X}",
        }
        for symbol in symbols.symbols
        if symbol.kind == "function"
    ]
    from genie.common import load_yaml

    sequence = load_yaml(scheduler_source)["call_sequence"]
    edges = [
        {
            "from": "0x001A8C16",
            "to": f"0x{parse_int(row['entry']):08X}",
            "site": f"0x{parse_int(row['call_site']):08X}",
        }
        for row in sequence
    ]
    xrefs = [
        {"from": "0x100", "to": f"0x{address:08X}", "type": "DATA"}
        for address in (0x1CBE, 0x1EBA, 0x4554)
    ]
    database = tmp_path / "full-rom"
    database.mkdir()
    documents = {
        "metadata.json": {
            "format": "openaladdin-ghidra-full-rom-v1",
            "rom_size": 2097152,
            "files": list(FILES),
        },
        "functions.json": functions,
        "instructions.json": [],
        "callgraph.json": {"edges": edges},
        "xrefs.json": {"references": xrefs},
        "memory_reads.json": {"references": []},
        "memory_writes.json": [{
            "from": "0x001A8C1E",
            "to": "0x00FF7E28",
            "type": "WRITE",
        }],
        "indirect_calls.json": {"references": []},
        "jump_tables.json": {"tables": []},
        "address_classes.json": {"classes": [{
            "start": "0x00000000",
            "end": "0x001FFFFF",
            "class": "UNKNOWN",
            "source": "test",
        }]},
    }
    for filename, value in documents.items():
        _write_json(database / filename, value)
    return database


def test_whole_rom_validator_accepts_known_database_facts(tmp_path):
    database = AnalysisDatabase(_write_validated_database_fixture(tmp_path))
    assert validate_database(database, root=tmp_path) == []


def test_whole_rom_validator_reports_missing_scheduler_fact(tmp_path):
    database_root = _write_validated_database_fixture(tmp_path)
    path = database_root / "callgraph.json"
    document = json.loads(path.read_text())
    document["edges"].pop(29)
    _write_json(path, document)

    errors = validate_database(AnalysisDatabase(database_root), root=tmp_path)
    assert any("direct scheduler calls" in error for error in errors)
