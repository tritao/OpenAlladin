from __future__ import annotations

import json
import shutil
from pathlib import Path

from genie.ghidra.database import AnalysisDatabase
from genie.ghidra.validate import validate_database
from genie.games.aladdin.vm.movement import MovementDecoder, load_animation_decoder
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


def test_layout_prefers_explicit_canonical_function_extent_over_sparse_ghidra_ranges(tmp_path):
    _write_empty_symbol_tree(tmp_path)
    (tmp_path / "re/symbols/functions.yml").write_text(
        """
0x00000010:
  name: CanonicalFunction
  size: 0x20
  confidence: decompiled
""",
        encoding="utf-8",
    )
    database_root = _write_database(tmp_path)
    _write_json(database_root / "functions.json", [{
        "address": "0x10",
        "name": "CanonicalFunction",
        "start": "0x10",
        "end": "0x2F",
        "ranges": [
            {"start": "0x10", "end": "0x17"},
            {"start": "0x28", "end": "0x2F"},
        ],
    }])

    layout = build_layout(AnalysisDatabase(database_root), root=tmp_path, include_artifacts=False)
    assert layout.at(0x20).layout_class == "CODE"
    assert layout.at(0x20).source == "tracked.symbol"
    assert layout.at(0x10).end == 0x2F


def test_layout_preserves_explicit_pointer_table_type_before_animation_name(tmp_path):
    _write_empty_symbol_tree(tmp_path)
    (tmp_path / "re/symbols/data.yml").write_text(
        """
0x00000020:
  name: PLAYER_ANIMATION_LOOKUP_TABLE
  type: rom_pointer_table
  entry_size: 4
  count: 2
""",
        encoding="utf-8",
    )
    database_root = _write_database(tmp_path)

    layout = build_layout(AnalysisDatabase(database_root), root=tmp_path, include_artifacts=False)
    assert layout.at(0x20).layout_class == "POINTER_TABLE"


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


def test_startup_font_glyph_bank_is_exact_and_precedes_reset_code():
    symbols = SymbolStore()
    padding = symbols.at(0x000004D0, include_ranges=False)
    assert padding is not None
    assert padding.name == "FONT_GLYPH_ALIGNMENT_PADDING"
    assert padding.end == 0x000004D7
    assert padding.size == 8
    assert padding.metadata["type"] == "padding_data"

    bank = symbols.at(0x000004D8, include_ranges=False)
    assert bank is not None
    assert bank.name == "FONT_GLYPH_DATA"
    assert bank.end == 0x000006A7
    assert bank.size == 464
    assert bank.metadata["type"] == "graphics_data"
    assert bank.metadata["entry_size"] == 8
    assert bank.metadata["count"] == 58

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    glyphs = rom_path.read_bytes()[bank.address:bank.end + 1]
    assert len(glyphs) == 58 * 8
    assert glyphs[:8] == bytes.fromhex("1818181800181800")


def test_unindexed_graphics_bands_and_padding_are_exact():
    symbols = SymbolStore()
    tile_band = symbols.at(0x0011E160, include_ranges=False)
    assert tile_band is not None
    assert tile_band.name == "UNINDEXED_SPRITE_TILE_DATA"
    assert tile_band.end == 0x0011EFFF
    assert tile_band.size == 0xEA0
    assert tile_band.metadata["type"] == "graphics_data"
    assert tile_band.confidence == "decompiled"

    padding = symbols.at(0x001A830A, include_ranges=False)
    assert padding is not None
    assert padding.name == "ROM_PADDING_ZERO_001A830A"
    assert padding.end == 0x001A831F
    assert padding.size == 0x16
    assert padding.metadata["type"] == "padding_data"
    assert padding.confidence == "confirmed"

    graphics = symbols.at(0x001A8320, include_ranges=False)
    assert graphics is not None
    assert graphics.name == "UNCOMPRESSED_GRAPHICS_BAND_001A8320"
    assert graphics.end == 0x001A8A49
    assert graphics.size == 0x72A
    assert graphics.metadata["type"] == "graphics_data"
    assert graphics.confidence == "decompiled"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = rom_path.read_bytes()
    assert len(rom[padding.address:padding.end + 1]) == padding.size
    assert set(rom[padding.address:padding.end + 1]) == {0}
    assert tile_band.size % 32 == 0
    assert rom[tile_band.address:tile_band.address + 16] == bytes.fromhex(
        "0F200000F4E20000F44400000F3F0000"
    )
    assert rom[graphics.address:graphics.address + 16] == bytes.fromhex(
        "0000AA000000AAA000007AAA00007AAA"
    )
    assert rom[graphics.end - 15:graphics.end + 1] == bytes.fromhex(
        "EAAA7777EAAAA77AEAAAAAAAEEAAAAAA"
    )


def test_genesis_header_tail_and_scene_stream_padding_are_exact():
    symbols = SymbolStore()
    header = symbols.at(0x000001A4, include_ranges=False)
    assert header is not None
    assert header.name == "ROM_SEGA_HEADER_EXTENDED_FIELDS"
    assert header.end == 0x000001FF
    assert header.size == 0x5C
    assert header.metadata["type"] == "rom_header"
    assert header.confidence == "confirmed"

    padding = symbols.at(0x001270A7, include_ranges=False)
    assert padding is not None
    assert padding.name == "SCENE_RESOURCE_STREAM_ALIGNMENT_PADDING"
    assert padding.end == 0x001270A7
    assert padding.size == 1
    assert padding.metadata["type"] == "padding_data"
    assert padding.confidence == "confirmed"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = rom_path.read_bytes()
    assert rom[0x001A4:0x001A8] == bytes.fromhex("001FFFFF")
    assert rom[0x001F0] == ord("U")
    assert rom[0x001F1:0x00200] == b" " * 15
    assert rom[padding.address] == 0


def test_render_vdp_word_and_hud_frame_sequence_are_exact():
    symbols = SymbolStore()
    vdp = symbols.at(0x00001CB2, include_ranges=False)
    assert vdp is not None
    assert vdp.name == "PLAYER_SPRITE_VDP_CONTROL_WORD"
    assert vdp.end == 0x00001CB5
    assert vdp.size == 4
    assert vdp.metadata["type"] == "vdp_control_word"
    assert vdp.confidence == "decompiled"

    sequence = symbols.at(0x000029A6, include_ranges=False)
    assert sequence is not None
    assert sequence.name == "HUD_INTERACTION_FRAME_SEQUENCE"
    assert sequence.end == 0x000029DF
    assert sequence.size == 0x3A
    assert sequence.metadata["type"] == "rom_table"
    assert sequence.metadata["entry_size"] == 2
    assert sequence.metadata["count"] == 28
    assert sequence.confidence == "decompiled"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = rom_path.read_bytes()
    assert rom[vdp.address:vdp.end + 1] == bytes.fromhex("74000003")
    assert rom[sequence.address:sequence.address + 4] == bytes.fromhex("E6AEE6AE")
    assert rom[sequence.end - 1:sequence.end + 1] == bytes.fromhex("0000")


def test_startup_region_warning_partition_is_exact():
    symbols = SymbolStore()

    routine = symbols.at(0x00000344, include_ranges=False)
    assert routine is not None
    assert routine.name == "System_DisplayRegionCompatibilityWarning"
    assert routine.end == 0x00000417
    assert routine.size == 212

    helper = symbols.at(0x00000418, include_ranges=False)
    assert helper is not None
    assert helper.name == "System_WriteRegionWarningText"
    assert helper.end == 0x0000044D
    assert helper.size == 54

    thunk = symbols.at(0x000006A8, include_ranges=False)
    assert thunk is not None
    assert thunk.name == "System_ResetToGameInitializationThunk"
    assert thunk.end == 0x000006AD
    assert thunk.size == 6

    initializer = symbols.at(0x001A8A4A, include_ranges=False)
    assert initializer is not None
    assert initializer.name == "Game_InitializeAndEnterFrameLoop"
    assert initializer.end == 0x001A8C15
    assert initializer.size == 460

    selector = symbols.at(0x0000044E, include_ranges=False)
    assert selector is not None
    assert selector.name == "SYSTEM_REGION_SELECTOR_BYTES"
    assert selector.end == 0x00000451
    assert selector.size == 4
    assert selector.metadata["type"] == "text_data"

    variants = symbols.at(0x00000452, include_ranges=False)
    assert variants is not None
    assert variants.name == "SYSTEM_REGION_WARNING_VARIANT_TABLE"
    assert variants.end == 0x00000465
    assert variants.size == 20
    assert variants.metadata["entry_size"] == 6
    assert variants.metadata["count"] == 3

    text = symbols.at(0x00000466, include_ranges=False)
    assert text is not None
    assert text.name == "SYSTEM_REGION_WARNING_TEXT"
    assert text.end == 0x000004CF
    assert text.size == 106


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


def test_runtime_type22_interaction_handler_is_canonical():
    symbols = SymbolStore()
    handler = symbols.at(0x001B6EEE, include_ranges=False)
    assert handler is not None
    assert handler.name == "InteractionSpawn_RuntimeType22_19"
    assert handler.confidence == "decompiled"
    assert "ACTOR_TEMPLATE_TYPE_1F" in handler.description
    assert "0x001238B2" in handler.description


def test_level_camera_scroll_callback_family_matches_level_table():
    symbols = SymbolStore()
    callbacks = {
        0x001AAA88: (395, "Level_CameraScrollCallback_Levels00_01_02", (0, 1, 2)),
        0x001AAC14: (385, "Level03_CameraScrollCallback", (3,)),
        0x001AAD96: (357, "Level10_CameraScrollCallback", (10,)),
        0x001AAEFC: (235, "Level08_CameraScrollCallback", (8,)),
        0x001AAFE8: (125, "Level_CameraScrollCallback_Levels11_12", (11, 12)),
        0x001AB066: (285, "Level07_CameraScrollCallback", (7,)),
        0x001AB184: (169, "Level09_CameraScrollCallback", (9,)),
        0x001AB22E: (141, "Level_CameraScrollCallback_Levels05_06", (5, 6)),
        0x001AB2BC: (145, "Level04_CameraScrollCallback", (4,)),
    }
    rom = Path("rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    seen_levels = set()
    for address, (size, name, levels) in callbacks.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == size
        assert symbol.end == address + size - 1
        for level in levels:
            record = 0x00002C78 + level * 66
            pointer = int.from_bytes(rom[record + 0x34:record + 0x38], "big")
            assert pointer == address
            seen_levels.add(level)
    assert seen_levels == set(range(13))


def test_menu_scene_and_input_data_extents_are_exact():
    symbols = SymbolStore()
    expected = {
        0x00004012: (0x6C, "MENU_CONTROL_LAYOUT_TABLE", "menu_control_layout_table"),
        0x00004082: (0x36, "INITIAL_SCENE_SCRIPT", "scene_script"),
        0x000040B8: (0x70, "SCENE_RESOURCE_VDP_STREAM_LEVEL08", "scene_resource_vdp_stream"),
        0x00004128: (0x12, "PRIMARY_INPUT_PATTERN_TABLE", "input_pattern_table"),
        0x0000413A: (0x1A, "ALTERNATE_INPUT_PATTERN_TABLE", "input_pattern_table"),
    }
    for address, (size, name, symbol_type) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == size
        assert symbol.end == address + size - 1
        assert symbol.metadata["type"] == symbol_type

    for address, name, offset in (
        (0x00004024, "MENU_CONTROL_LAYOUT_VARIANT_A_C_B", 0x12),
        (0x00004036, "DEFAULT_MENU_CONTROL_LAYOUT", 0x24),
        (0x00004048, "MENU_CONTROL_LAYOUT_VARIANT_B_C_A", 0x36),
        (0x0000405A, "MENU_CONTROL_LAYOUT_VARIANT_C_A_B", 0x48),
        (0x0000406C, "MENU_CONTROL_LAYOUT_VARIANT_C_B_A", 0x5A),
    ):
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.metadata["alias_of"] == "MENU_CONTROL_LAYOUT_TABLE"
        assert symbol.metadata["entry_offset"] == offset

    rom = Path("rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x4138:0x413A] == bytes.fromhex("FF00")
    assert rom[0x4152:0x4154] == bytes.fromhex("FF00")


def test_canonical_exception_vector_table_has_complete_rom_header_extent():
    symbols = SymbolStore()
    table = symbols.at(0x00000000, include_ranges=False)
    assert table is not None
    assert table.name == "SYSTEM_EXCEPTION_VECTOR_TABLE"
    assert table.end == 0x000000FF
    assert table.size == 0x100
    assert table.metadata["type"] == "rom_pointer_table"
    assert table.metadata["entry_size"] == 4
    assert table.metadata["count"] == 64

    rom = Path("rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert int.from_bytes(rom[0x00:0x04], "big") == 0x00FFEFD8
    assert int.from_bytes(rom[0x04:0x08], "big") == 0x0000021A
    assert int.from_bytes(rom[0x70:0x74], "big") == 0x001B249C
    assert rom[0xC0:0x100] == bytes(0x40)


def test_canonical_scene_resource_object_animation_table_has_exact_extent():
    symbols = SymbolStore()
    table = symbols.at(0x00004A18, include_ranges=False)
    assert table is not None
    assert table.name == "SCENE_RESOURCE_OBJECT_ANIMATION_TABLE"
    assert table.end == 0x00004A57
    assert table.size == 0x40
    assert table.metadata["type"] == "rom_pointer_table"
    assert table.metadata["entry_size"] == 4
    assert table.metadata["count"] == 16

    rom = Path("rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    pointers = [
        int.from_bytes(rom[offset:offset + 4], "big")
        for offset in range(0x4A18, 0x4A58, 4)
    ]
    assert pointers == [
        0x00000000, 0x00122D40, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00122D4C,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00122D50, 0x00000000, 0x00122D44, 0x00000000,
    ]


def test_actor_frame_phase_child_animation_table_has_exact_extent():
    symbols = SymbolStore()
    table = symbols.at(0x000049A9, include_ranges=False)
    assert table is not None
    assert table.name == "ACTOR_FRAME_PHASE_CHILD_ANIMATION_TABLE"
    assert table.end == 0x000049C0
    assert table.size == 0x18
    assert table.metadata["type"] == "actor_spawn_phase_animation_table"
    assert table.metadata["entry_size"] == 6
    assert table.metadata["count"] == 4

    rom = Path("rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    records = [rom[offset:offset + 6] for offset in range(0x49A9, 0x49C1, 6)]
    assert records == [
        bytes.fromhex("000000000000"),
        bytes.fromhex("4000122C1200"),
        bytes.fromhex("000000000000"),
        bytes.fromhex("4000122C1200"),
    ]


def test_level08_rotating_vdp_record_table_has_exact_extent():
    symbols = SymbolStore()
    table = symbols.at(0x000029E0, include_ranges=False)
    assert table is not None
    assert table.name == "LEVEL08_ROTATING_VDP_RECORD_TABLE"
    assert table.end == 0x00002A3F
    assert table.size == 0x60
    assert table.metadata["type"] == "scene_resource_vdp_record_table"
    assert table.metadata["entry_size"] == 6
    assert table.metadata["count"] == 16

    rom = Path("rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    records = [rom[offset:offset + 6] for offset in range(0x29E0, 0x2A40, 6)]
    assert len(records) == 16
    assert records[0] == bytes.fromhex("C0020000006E")
    assert records[-1] == bytes.fromhex("C01A00000008")
    assert rom[0x2A40:0x2A48] == bytes.fromhex("0001000080100001")


def test_scene_vdp_fixed_data_has_exact_extents():
    symbols = SymbolStore()
    header = symbols.at(0x00002A40, include_ranges=False)
    assert header is not None
    assert header.name == "SCENE_HEADER_VDP_WORDS"
    assert header.end == 0x00002A47
    assert header.size == 8
    assert header.metadata["type"] == "scene_resource_vdp_header"
    assert header.metadata["entry_size"] == 2
    assert header.metadata["count"] == 4

    stream = symbols.at(0x00002A48, include_ranges=False)
    assert stream is not None
    assert stream.name == "SCENE_VDP_INDIRECT_ZERO_FILL_STREAM"
    assert stream.end == 0x00002A51
    assert stream.size == 0x0A
    assert stream.metadata["type"] == "scene_resource_vdp_indirect_stream"

    rom = Path("rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x2A40:0x2A48] == bytes.fromhex("0001000080100001")
    assert rom[0x2A48:0x2A52] == bytes.fromhex("00000000FFFF00002A48")


def test_scene_vdp_transition_plane_offset_table_has_exact_extent():
    symbols = SymbolStore()
    table = symbols.at(0x00002080, include_ranges=False)
    assert table is not None
    assert table.name == "SCENE_VDP_TRANSITION_PLANE_OFFSET_TABLE"
    assert table.end == 0x000020BF
    assert table.size == 0x40
    assert table.metadata["type"] == "scene_resource_vdp_offset_table"
    assert table.metadata["entry_size"] == 2
    assert table.metadata["count"] == 32

    rom = Path("rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    words = [
        int.from_bytes(rom[offset:offset + 2], "big")
        for offset in range(0x2080, 0x20C0, 2)
    ]
    assert words == [
        0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0000, 0x0000, 0x0000,
        0x0001, 0x0000, 0x0000, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0xFFFF, 0x0000, 0x0000, 0xFFFF,
        0x0000, 0x0000, 0x0000, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000,
    ]


def test_scene_vdp_transition_padding_has_exact_extent():
    symbols = SymbolStore()
    padding = symbols.at(0x00002022, include_ranges=False)
    assert padding is not None
    assert padding.name == "ROM_PADDING_FF_002022"
    assert padding.end == 0x0000207F
    assert padding.size == 0x5E
    assert padding.metadata["type"] == "padding_data"

    rom = Path("rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x2022:0x2080] == bytes([0xFF]) * 0x5E


def test_interaction_counter_animation_table_and_bank_are_exact():
    symbols = SymbolStore()
    table = symbols.at(0x00004A58, include_ranges=False)
    assert table is not None
    assert table.name == "INTERACTION_COUNTER_ANIMATION_ROOT_TABLE"
    assert table.size == 172
    assert table.end == 0x00004B03
    assert table.metadata["type"] == "rom_pointer_table"
    assert table.metadata["entry_size"] == 4
    assert table.metadata["count"] == 43

    bank = symbols.at(0x00122CAC, include_ranges=False)
    assert bank is not None
    assert bank.name == "ACTOR_ANIM_INTERACTION_COUNTER_FRAME_BANK"
    assert bank.size == 168
    assert bank.end == 0x00122D53
    assert bank.metadata["type"] == "animation_stream"

    following = symbols.at(0x00122D54, include_ranges=False)
    assert following is not None
    assert following.name == "ACTOR_ANIM_TYPE55_INTERACTION"
    assert bank.end + 1 == following.address


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


def test_player_action_transition_child_animation_is_exact():
    symbols = SymbolStore()
    symbol = symbols.at(0x00122B58, include_ranges=False)
    assert symbol is not None
    assert symbol.name == "ACTOR_ANIM_TYPE80_ACTION_TRANSITION_CHILD"
    assert symbol.end == 0x00122B6D
    assert symbol.size == 22
    assert symbol.metadata["type"] == "animation_stream"
    collision_response = symbols.at(0x00122B6E, include_ranges=False)
    assert collision_response is not None
    assert collision_response.name == "ACTOR_ANIM_TYPE84_TYPE2D2E31_COLLISION_RESPONSE"


def test_type10_collision_response_animation_is_exact():
    symbols = SymbolStore()
    response = symbols.at(0x001239A0, include_ranges=False)
    assert response is not None
    assert response.name == "ACTOR_ANIM_TYPE10_COLLISION_RESPONSE"
    assert response.end == 0x001239C9
    assert response.size == 42
    assert response.metadata["type"] == "animation_stream"

    continuation = symbols.at(0x001239DE, include_ranges=False)
    assert continuation is not None
    assert continuation.name == "ACTOR_ANIM_TYPE10_COLLISION_RESPONSE_CONTINUATION"
    assert continuation.metadata["alias_of"] == "ACTOR_ANIM_TYPE10_INTERACTION_RESPONSE"
    assert continuation.metadata["entry_offset"] == 20


def test_mid_actor_collision_animation_family_is_exact():
    symbols = SymbolStore()
    expected = {
        0x00122B6E: (0x00122BBB, "ACTOR_ANIM_TYPE84_TYPE2D2E31_COLLISION_RESPONSE"),
        0x00122BBC: (0x00122BD7, "ACTOR_ANIM_TYPE84_COLLISION_CHILD_VARIANT_B"),
        0x00122BD8: (0x00122C11, "ACTOR_ANIM_TYPE3A_3B_INTERACTION_RESPONSE"),
        0x00122C12: (0x00122C1D, "ACTOR_ANIM_TYPE40_INTERACTION"),
    }
    owners = []
    for address, (end, name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        assert symbol.metadata["type"] == "animation_stream"
        owners.append((symbol.address, symbol.end))
    assert all(right + 1 == next_left for (_, right), (next_left, _) in zip(owners, owners[1:]))
    assert symbols.at(0x00122C1E, include_ranges=False).name == "ACTOR_ANIM_TYPE_34_WALL"

    child = symbols.at(0x00122B9C, include_ranges=False)
    assert child is not None
    assert child.name == "ACTOR_ANIM_TYPE84_COLLISION_CHILD_VARIANT_A"
    assert child.metadata["alias_of"] == "ACTOR_ANIM_TYPE84_TYPE2D2E31_COLLISION_RESPONSE"
    assert child.metadata["entry_offset"] == 46

    template = symbols.at(0x001B79A4, include_ranges=False)
    assert template is not None
    assert template.name == "ACTOR_TEMPLATE_TYPE84_COLLISION_CHILD"
    assert template.end == 0x001B79B7
    assert template.size == 20


def test_direct_movement_response_streams_are_exact():
    symbols = SymbolStore()
    expected = {
        0x001209BE: (0x001209C5, "ACTOR_MOVE_TYPE7F_PLAYER_COLLISION_RESPONSE"),
        0x001209F0: (0x001209F7, "ACTOR_MOVE_TYPE84_RANDOM_VARIANT_A"),
        0x001209F8: (0x001209FF, "ACTOR_MOVE_TYPE84_RANDOM_VARIANT_B"),
        0x00120A42: (0x00120ACB, "ACTOR_MOVE_TYPE75_LEVEL_EXIT"),
    }
    for address, (end, name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        assert symbol.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = MovementDecoder(rom)
    for address, (end, _) in expected.items():
        decoded = decoder.decode_stream(
            address,
            max_steps=256,
            max_bytes=512,
            follow_control_flow=True,
        )
        assert decoded["bytes_decoded"] == end - address + 1
        assert decoded["stopped_reason"] == "control_flow_cycle"


def test_type07_and_type13_movement_partition_is_exact():
    symbols = SymbolStore()
    type07 = symbols.at(0x001216C6, include_ranges=False)
    type13 = symbols.at(0x001216DC, include_ranges=False)
    tail = symbols.at(0x00121706, include_ranges=False)
    assert type07 is not None and type07.name == "ACTOR_MOVE_TYPE07_MOVING_INTERACTION"
    assert (type07.address, type07.end, type07.size) == (0x001216C6, 0x001216DB, 22)
    assert type13 is not None and type13.name == "ACTOR_MOVE_TYPE13_INTERACTION_RESPONSE"
    assert (type13.address, type13.end, type13.size) == (0x001216DC, 0x00121705, 42)
    assert tail is not None and tail.name == "ACTOR_MOVE_TYPE13_INTERACTION_RESPONSE_SHARED_TAIL"
    assert (tail.address, tail.end, tail.size) == (0x00121706, 0x0012170F, 10)
    assert type07.end + 1 == type13.address
    assert type13.end + 1 == tail.address

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    decoder = MovementDecoder(load_animation_decoder().RomReader(rom_path.read_bytes()))
    for address, size, end in (
        (0x001216C6, 22, 0x001216DB),
        (0x001216DC, 42, 0x00121705),
        (0x00121706, 10, 0x0012170F),
    ):
        decoded = decoder.decode_stream(
            address,
            max_steps=256,
            max_bytes=size,
            follow_control_flow=False,
            continue_after_control_flow=True,
        )
        assert decoded["bytes_decoded"] == size
        assert decoded["stopped_reason"] == "byte_limit"
        assert decoded["steps"][-1]["next_address"] == f"0x{end + 1:08X}"


def test_runtime_type6e_73_movement_family_is_exact():
    symbols = SymbolStore()
    expected = [
        (0x00120584, 0x001205C9, "ACTOR_MOVE_TYPE6E_INTERACTION", 70),
        (0x001205F4, 0x00120639, "ACTOR_MOVE_TYPE6F_INTERACTION", 70),
        (0x00120664, 0x001206BB, "ACTOR_MOVE_TYPE70_INTERACTION", 88),
        (0x0012070E, 0x0012075D, "ACTOR_MOVE_TYPE71_INTERACTION", 80),
        (0x00120868, 0x001208AD, "ACTOR_MOVE_TYPE72_INTERACTION", 70),
        (0x001208D8, 0x0012091D, "ACTOR_MOVE_TYPE73_INTERACTION", 70),
        (0x00120948, 0x001209BD, "ACTOR_MOVE_RUNTIME_TYPE6E_73_SHARED_RESPONSE", 118),
    ]
    owners = []
    for address, end, name, size in expected:
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        assert symbol.size == size
        assert symbol.metadata["type"] == "movement_stream"
        owners.append((symbol.address, symbol.end))

    assert all(right < next_left for (_, right), (next_left, _) in zip(owners, owners[1:]))

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    decoder = MovementDecoder(load_animation_decoder().RomReader(rom_path.read_bytes()))
    for address, end, _, size in expected:
        decoded = decoder.decode_stream(
            address,
            max_steps=512,
            max_bytes=size,
            follow_control_flow=False,
        )
        assert decoded["bytes_decoded"] == size
        assert decoded["steps"][-1]["next_address"] == f"0x{end + 1:08X}"


def test_runtime_type6e_73_secondary_movement_family_is_exact():
    symbols = SymbolStore()
    expected = [
        (0x001205CA, 0x001205F3, "ACTOR_MOVE_TYPE6E_INTERACTION_SECONDARY", 42),
        (0x0012063A, 0x00120663, "ACTOR_MOVE_TYPE6F_INTERACTION_SECONDARY", 42),
        (0x001206BC, 0x0012070D, "ACTOR_MOVE_TYPE70_INTERACTION_SECONDARY", 82),
        (0x0012075E, 0x00120867, "ACTOR_MOVE_TYPE71_INTERACTION_SECONDARY", 266),
        (0x001208AE, 0x001208D7, "ACTOR_MOVE_TYPE72_INTERACTION_SECONDARY", 42),
        (0x0012091E, 0x00120947, "ACTOR_MOVE_TYPE73_INTERACTION_SECONDARY", 42),
    ]
    owners = []
    for address, end, name, size in expected:
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        assert symbol.size == size
        assert symbol.metadata["type"] == "movement_stream"
        owners.append((symbol.address, symbol.end))

    assert all(right < next_left for (_, right), (next_left, _) in zip(owners, owners[1:]))

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    decoder = MovementDecoder(load_animation_decoder().RomReader(rom_path.read_bytes()))
    for address, end, _, size in expected:
        decoded = decoder.decode_stream(
            address,
            max_steps=1024,
            max_bytes=size,
            follow_control_flow=False,
        )
        assert decoded["bytes_decoded"] == size
        assert decoded["steps"][-1]["next_address"] == f"0x{end + 1:08X}"


def test_type5e84_e1e2_movement_stream_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x0011F8A4, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_MOVE_TYPE5E84_PAIR_E1E2"
    assert stream.end == 0x0011FAA7
    assert stream.size == 0x204
    assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    decoder = MovementDecoder(load_animation_decoder().RomReader(rom_path.read_bytes()))
    decoded = decoder.decode_stream(
        0x0011F8A4,
        max_steps=512,
        max_bytes=0x204,
        follow_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 0x204
    assert decoded["stopped_reason"] == "byte_limit"
    assert decoded["steps"][-1]["next_address"] == "0x0011FAA8"


def test_unindexed_movement_stream_bands_are_exact_with_evidence_confidence():
    symbols = SymbolStore()
    expected = [
        (0x0011FAA8, 0x0011FD17, "ACTOR_MOVE_TYPE5E84_PAIR_ANCHOR_RESPONSE", 0x270, "decompiled"),
        (0x001210FE, 0x0012117F, "ACTOR_MOVE_FLAG20_TERRAIN_RESPONSE", 0x82, "decompiled"),
        (0x001212C0, 0x001212FF, "ACTOR_MOVE_INTERACTION_ANCHOR_RESPONSE_BANK", 0x40, "decompiled"),
        (0x001213E2, 0x00121411, "ACTOR_MOVE_TYPE8D_WALL_RESPONSE_CHILD_SPAWN_PREFIX", 0x30, "decompiled"),
        (0x001215D8, 0x001215DF, "ACTOR_MOVE_UNIT_VERTICAL_STEP_LOOP", 8, "decompiled"),
    ]
    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    decoder = MovementDecoder(load_animation_decoder().RomReader(rom_path.read_bytes()))
    for address, end, name, size, confidence in expected:
        stream = symbols.at(address, include_ranges=False)
        assert stream is not None
        assert stream.name == name
        assert stream.end == end
        assert stream.size == size
        assert stream.metadata["type"] == "movement_stream"
        assert stream.confidence == confidence

        decoded = decoder.decode_stream(
            address,
            max_steps=1024,
            max_bytes=size,
            follow_control_flow=False,
        )
        assert decoded["bytes_decoded"] == size
        assert decoded["stopped_reason"] == "byte_limit"
        assert decoded["steps"][-1]["next_address"] == f"0x{end + 1:08X}"


def test_type5e84_anchor_response_bank_preserves_anchor_and_grid_evidence():
    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    decoder = MovementDecoder(load_animation_decoder().RomReader(rom_path.read_bytes()))
    decoded = decoder.decode_stream(
        0x0011FAA8,
        max_steps=1024,
        max_bytes=0x270,
        follow_control_flow=False,
    )

    assert decoded["bytes_decoded"] == 0x270
    assert decoded["stopped_reason"] == "byte_limit"
    assert decoded["steps"][-1]["next_address"] == "0x0011FD18"

    commands = [
        command
        for step in decoded["steps"]
        for command in step.get("commands", [])
    ]
    parameters = [command["parameter"] for command in commands if command.get("parameter")]
    assert len(parameters) == 51
    assert set(parameters) == {"0x001B58BA"}

    opening_compare = next(command for command in commands if command.get("name") == "if_compare")
    assert opening_compare["compare_fields"] == ["0x21", "0xF0", "0xD3"]
    assert opening_compare["compare_value"] == "005E"
    assert opening_compare["branch_target"] == "0x0011FAA8"

    timers = [
        command["value"]
        for command in commands
        if command.get("name") == "set_frame_timer_or_field"
    ]
    assert len(timers) == 50
    assert set(timers) == {"0x93"}

    terminal_jump = next(command for command in commands if command.get("name") == "jump")
    assert terminal_jump["branch_target"] == "0x0011F890"


def test_flag20_terrain_response_stream_preserves_callback_and_terrain_evidence():
    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    decoder = MovementDecoder(load_animation_decoder().RomReader(rom_path.read_bytes()))
    decoded = decoder.decode_stream(
        0x001210FE,
        max_steps=1024,
        max_bytes=0x82,
        follow_control_flow=False,
    )

    assert decoded["bytes_decoded"] == 0x82
    commands = [
        command
        for step in decoded["steps"]
        for command in step.get("commands", [])
    ]
    parameters = [command.get("parameter") for command in commands]
    assert parameters.count("0x001ACB7A") == 2
    assert parameters.count("0x001ACB82") == 2

    compares = [command for command in commands if command.get("name") == "if_compare"]
    assert len(compares) == 2
    assert all(compare["compare_fields"] == ["0x24", "0xF0", "0x7C"] for compare in compares)

    timer_values = [
        command["value"]
        for command in commands
        if command.get("name") == "set_frame_timer_or_field"
    ]
    assert timer_values[:3] == ["0x87", "0x13", "0x07"]
    assert len(timer_values) == 15
    assert timer_values[3:] == ["0x83"] * 12

    assert decoded["steps"][0]["delta_x"] == -124
    assert decoded["steps"][0]["delta_y"] == -113
    assert decoded["steps"][-1]["delta_x"] == -12
    assert decoded["steps"][-1]["delta_y"] == 0
    assert any(command.get("name") == "destroy_or_clear_actor" for command in commands)


def test_type5e84_pair_movement_stream_family_is_exact_and_contiguous():
    symbols = SymbolStore()
    expected = [
        (0x0011FD18, 0x0012004D, "ACTOR_MOVE_TYPE5E84_PAIR_E3E5", 822, 0x00120048),
        (0x0012004E, 0x001200DD, "ACTOR_MOVE_TYPE5E84_PAIR_E6", 144, 0x001200D8),
        (0x001200DE, 0x001201FD, "ACTOR_MOVE_TYPE5E84_PAIR_F9", 288, 0x001201F8),
        (0x001201FE, 0x001202C9, "ACTOR_MOVE_TYPE5E84_PAIR_E8", 204, 0x001202C4),
        (0x001202CA, 0x00120351, "ACTOR_MOVE_TYPE5E84_PAIR_E9", 136, 0x0012034C),
    ]
    previous_end = None
    for address, end, name, size, terminal_jump in expected:
        stream = symbols.at(address, include_ranges=False)
        assert stream is not None
        assert stream.name == name
        assert stream.end == end
        assert stream.size == size
        assert stream.metadata["type"] == "movement_stream"
        if previous_end is not None:
            assert address == previous_end + 1
        previous_end = end

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = MovementDecoder(rom)
    for address, end, _, size, terminal_jump in expected:
        decoded = decoder.decode_stream(
            address,
            max_steps=1024,
            max_bytes=4096,
            follow_control_flow=True,
        )
        assert decoded["stopped_reason"] == "control_flow_cycle"
        # The decoder also records the eight-byte shared continuation reached
        # by the terminal jump; local ownership ends before that continuation.
        assert decoded["bytes_decoded"] == size + 8
        terminal = decoded["steps"][-1]
        jump = next(command for command in terminal["commands"] if command["opcode"] == "0x80")
        assert int(jump["address"], 16) == terminal_jump
        assert int(jump["address"], 16) + int(jump["size"]) == end + 1
        assert jump["branch_target"] == "0x0011F890"


def test_type84_death_terminal_movement_stream_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x00120352, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_MOVE_TYPE84_DEATH_TERMINAL"
    assert stream.end == 0x0012035F
    assert stream.size == 14
    assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoded = MovementDecoder(rom).decode_stream(
        0x00120352,
        max_steps=256,
        max_bytes=512,
        follow_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 14
    assert decoded["stopped_reason"] == "control_flow_cycle"
    assert decoded["steps"][-1]["address"] == "0x00120352"
    jump = next(command for command in decoded["steps"][-1]["commands"] if command["opcode"] == "0x80")
    assert jump["branch_target"] == "0x00120352"


def test_type31_f5_child_movement_stream_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x00120B62, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_MOVE_TYPE31_F5_CHILD_SHARED"
    assert stream.end == 0x00120D79
    assert stream.size == 536
    assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = MovementDecoder(rom)
    linear = decoder.decode_stream(
        0x00120B62,
        max_steps=1024,
        max_bytes=536,
        follow_control_flow=False,
    )
    assert linear["bytes_decoded"] == 536
    assert linear["stopped_reason"] == "byte_limit"
    assert linear["steps"][-1]["next_address"] == "0x00120D7A"

    decoded = decoder.decode_stream(
        0x00120B62,
        max_steps=1024,
        max_bytes=4096,
        follow_control_flow=True,
    )
    assert decoded["stopped_reason"] == "control_flow_cycle"
    commands = [command for step in decoded["steps"] for command in step["commands"]]
    targets = {command.get("branch_target") for command in commands}
    assert "0x00120D6A" in targets
    assert "0x00120D72" in targets
    assert "0x00120D4C" in targets


def test_type84_runtime47_4c_child_movement_stream_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x00121256, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_MOVE_TYPE84_RUNTIME47_4C_CHILD_SHARED"
    assert stream.end == 0x001212BF
    assert stream.size == 106
    assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = MovementDecoder(rom)
    linear = decoder.decode_stream(
        0x00121256,
        max_steps=256,
        max_bytes=106,
        follow_control_flow=False,
    )
    assert linear["bytes_decoded"] == 106
    assert linear["stopped_reason"] == "byte_limit"
    assert linear["steps"][-1]["next_address"] == "0x001212C0"

    decoded = decoder.decode_stream(
        0x00121256,
        max_steps=256,
        max_bytes=512,
        follow_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 54
    assert decoded["stopped_reason"] == "control_flow_cycle"
    assert decoded["steps"][-1]["next_address"] == "0x0012127C"
    commands = [command for step in decoded["steps"] for command in step["commands"]]
    assert {command.get("branch_target") for command in commands} >= {
        "0x0012127C",
        "0x0012128C",
    }


def test_type52_level09_movement_stream_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x00121300, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_MOVE_TYPE52_LEVEL09_ENTRY"
    assert stream.end == 0x001213E1
    assert stream.size == 226
    assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = MovementDecoder(rom)
    linear = decoder.decode_stream(
        0x00121300,
        max_steps=512,
        max_bytes=226,
        follow_control_flow=False,
    )
    assert linear["bytes_decoded"] == 226
    assert linear["stopped_reason"] == "byte_limit"
    assert linear["steps"][-1]["next_address"] == "0x001213E2"

    decoded = decoder.decode_stream(
        0x00121300,
        max_steps=512,
        max_bytes=1024,
        follow_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 226
    assert decoded["stopped_reason"] == "control_flow_cycle"
    assert decoded["steps"][-1]["address"] == "0x001213DA"
    jump = next(command for command in decoded["steps"][-1]["commands"] if command["opcode"] == "0x80")
    assert jump["branch_target"] == "0x00121300"


def test_type64_movement_stream_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x00120B36, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_MOVE_TYPE64_INTERACTION"
    assert stream.end == 0x00120B61
    assert stream.size == 44
    assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = MovementDecoder(rom)
    linear = decoder.decode_stream(
        0x00120B36,
        max_steps=256,
        max_bytes=44,
        follow_control_flow=False,
    )
    assert linear["bytes_decoded"] == 44
    assert linear["stopped_reason"] == "byte_limit"
    assert linear["steps"][-1]["next_address"] == "0x00120B62"

    decoded = decoder.decode_stream(
        0x00120B36,
        max_steps=256,
        max_bytes=512,
        follow_control_flow=True,
    )
    assert decoded["stopped_reason"] == "control_flow_cycle"
    jump = next(
        command
        for step in decoded["steps"]
        for command in step["commands"]
        if command["opcode"] == "0x80"
    )
    assert jump["branch_target"] == "0x00120B38"


def test_type2f_interaction_movement_family_is_exact():
    symbols = SymbolStore()
    expected = {
        0x00120ACC: (0x00120B13, 72, "ACTOR_MOVE_TYPE2F_INTERACTION_RESPONSE"),
        0x00120B14: (0x00120B1F, 12, "ACTOR_MOVE_TYPE2F_INTERACTION_STATE84_ENTRY"),
        0x00120B20: (0x00120B2B, 12, "ACTOR_MOVE_TYPE2F_INTERACTION_STATE83_ENTRY"),
        0x00120B2C: (0x00120B35, 10, "ACTOR_MOVE_TYPE2F_INTERACTION_STATE87_TRANSITION"),
    }
    for address, (end, size, name) in expected.items():
        stream = symbols.at(address, include_ranges=False)
        assert stream is not None
        assert stream.name == name
        assert stream.end == end
        assert stream.size == size
        assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = MovementDecoder(rom)
    for address, (end, size, _) in expected.items():
        linear = decoder.decode_stream(
            address,
            max_steps=256,
            max_bytes=size,
            follow_control_flow=False,
        )
        assert linear["bytes_decoded"] == size
        assert linear["stopped_reason"] == "byte_limit"
        assert linear["steps"][-1]["next_address"] == f"0x{end + 1:08X}"

        decoded = decoder.decode_stream(
            address,
            max_steps=256,
            max_bytes=512,
            follow_control_flow=True,
        )
        expected_flow = 56 if address == 0x00120B2C else size
        assert decoded["bytes_decoded"] == expected_flow
        assert decoded["stopped_reason"] == "control_flow_cycle"

    root = decoder.decode_stream(0x00120ACC, 256, 512, True)
    targets = {
        command["branch_target"]
        for step in root["steps"]
        for command in step["commands"]
        if command.get("branch_target")
    }
    assert {
        "0x00120B14",
        "0x00120B20",
        "0x00120B2C",
    } <= targets


def test_type7b_level11_movement_stream_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x0012120E, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_MOVE_TYPE7B_LEVEL11_EVENT"
    assert stream.end == 0x00121225
    assert stream.size == 24
    assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = MovementDecoder(rom)
    linear = decoder.decode_stream(
        0x0012120E,
        max_steps=256,
        max_bytes=24,
        follow_control_flow=False,
    )
    assert linear["bytes_decoded"] == 24
    assert linear["stopped_reason"] == "byte_limit"
    assert linear["steps"][-1]["next_address"] == "0x00121226"

    decoded = decoder.decode_stream(
        0x0012120E,
        max_steps=256,
        max_bytes=512,
        follow_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 24
    assert decoded["stopped_reason"] == "control_flow_cycle"
    jumps = [
        command
        for step in decoded["steps"]
        for command in step["commands"]
        if command["opcode"] == "0x80"
    ]
    assert jumps[-1]["branch_target"] == "0x0012120E"


def test_type7b_level11_movement_alternate_entries_are_exact():
    symbols = SymbolStore()
    expected = {
        0x00121226: (0x0012123D, 24, "ACTOR_MOVE_TYPE7B_LEVEL11_EVENT_DISTANCE_ENTRY"),
        0x0012123E: (0x0012123F, 2, "ACTOR_MOVE_TYPE7B_LEVEL11_EVENT_COMPARE_TRANSITION"),
    }
    for address, (end, size, name) in expected.items():
        stream = symbols.at(address, include_ranges=False)
        assert stream is not None
        assert stream.name == name
        assert stream.end == end
        assert stream.size == size
        assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = MovementDecoder(rom)
    for address, (end, size, _) in expected.items():
        linear = decoder.decode_stream(
            address,
            max_steps=256,
            max_bytes=size,
            follow_control_flow=False,
        )
        assert linear["bytes_decoded"] == size
        assert linear["stopped_reason"] == "byte_limit"
        assert linear["steps"][-1]["next_address"] == f"0x{end + 1:08X}"

    distance = decoder.decode_stream(0x00121226, 256, 512, True)
    assert distance["bytes_decoded"] == 24
    assert distance["stopped_reason"] == "control_flow_cycle"
    distance_jump = next(
        command
        for step in distance["steps"]
        for command in step["commands"]
        if command["opcode"] == "0x80"
    )
    assert distance_jump["branch_target"] == "0x0012120E"

    transition = decoder.decode_stream(0x0012123E, 256, 512, True)
    assert transition["bytes_decoded"] == 24
    assert transition["stopped_reason"] == "control_flow_cycle"


def test_type7c_type7d_level_event_movement_family_is_exact():
    symbols = SymbolStore()
    prelude = symbols.at(0x00121180, include_ranges=False)
    assert prelude is not None
    assert prelude.name == "ACTOR_MOVE_TYPE7C_WIDE_RANDOM_OFFSETS_PRELUDE"
    assert prelude.end == 0x00121189
    assert prelude.size == 10
    assert prelude.metadata["type"] == "movement_stream"
    assert prelude.confidence == "decompiled"

    shared = symbols.at(0x0012118A, include_ranges=False)
    assert shared is not None
    assert shared.name == "ACTOR_MOVE_TYPE7C_LEVEL_EVENT_SHARED"
    assert shared.end == 0x0012120D
    assert shared.size == 132
    assert shared.metadata["type"] == "movement_stream"

    alias = symbols.at(0x001211C4, include_ranges=False)
    assert alias is not None
    assert alias.name == "ACTOR_MOVE_TYPE7D_LEVEL_ENTRY"
    assert alias.metadata["alias_of"] == "ACTOR_MOVE_TYPE7C_LEVEL_EVENT_SHARED"
    assert alias.metadata["entry_offset"] == 58

    template = symbols.at(0x001B81B0, include_ranges=False)
    assert template is not None
    assert template.name == "ACTOR_TEMPLATE_TYPE_7C_WIDE_RANDOM_EVENT"
    assert template.end == 0x001B81C3
    assert template.size == 20
    assert template.metadata["type"] == "actor_template"
    assert template.confidence == "provisional"

    animation = symbols.at(0x00125916, include_ranges=False)
    assert animation is not None
    assert animation.name == "ACTOR_ANIM_TYPE7C_LEVEL_EVENT_SHARED"
    assert animation.end == 0x0012593F
    assert animation.size == 42

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom_bytes = rom_path.read_bytes()
    assert rom_bytes[template.address:template.end + 1] == bytes.fromhex(
        "7C00400000000012118020000012591603001000"
    )
    rom = load_animation_decoder().RomReader(rom_bytes)
    decoder = MovementDecoder(rom)
    prelude_decode = decoder.decode_stream(
        0x00121180,
        max_steps=256,
        max_bytes=10,
        follow_control_flow=False,
    )
    assert prelude_decode["bytes_decoded"] == 10
    assert prelude_decode["stopped_reason"] == "byte_limit"
    assert prelude_decode["steps"][0]["delta_x"] == 0
    assert prelude_decode["steps"][0]["delta_y"] == -4
    assert [command["name"] for command in prelude_decode["steps"][0]["commands"]] == [
        "push_parameter",
        "clear_actor_state",
    ]
    assert prelude_decode["steps"][0]["commands"][0]["parameter"] == "0x001ACD5A"
    assert prelude_decode["steps"][-1]["next_address"] == "0x0012118A"

    shared_decode = decoder.decode_stream(
        0x0012118A,
        max_steps=256,
        max_bytes=132,
        follow_control_flow=False,
    )
    assert shared_decode["bytes_decoded"] == 132
    assert shared_decode["stopped_reason"] == "byte_limit"
    assert shared_decode["steps"][-1]["next_address"] == "0x0012120E"

    expected_bytes = {
        0x0012118A: 132,
        0x001211C4: 74,
    }
    for address, byte_count in expected_bytes.items():
        decoded = decoder.decode_stream(
            address,
            max_steps=256,
            max_bytes=512,
            follow_control_flow=True,
        )
        assert decoded["bytes_decoded"] == byte_count
        assert decoded["stopped_reason"] == "control_flow_cycle"


def test_menu_presentation_child_movement_prefixes_are_exact():
    symbols = SymbolStore()
    expected = {
        0x00121684: (0x001216A9, 38, "ACTOR_MOVE_MENU_PRESENTATION_CHILD_A"),
        0x001216AA: (0x001216C5, 28, "ACTOR_MOVE_MENU_PRESENTATION_CHILD_B"),
    }
    for address, (end, size, name) in expected.items():
        stream = symbols.at(address, include_ranges=False)
        assert stream is not None
        assert stream.name == name
        assert stream.end == end
        assert stream.size == size
        assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = MovementDecoder(rom)
    for address, (end, size, _) in expected.items():
        decoded = decoder.decode_stream(
            address,
            max_steps=256,
            max_bytes=size,
            follow_control_flow=False,
        )
        assert decoded["bytes_decoded"] == size
        assert decoded["stopped_reason"] == "byte_limit"
        assert decoded["steps"][-1]["next_address"] == f"0x{end + 1:08X}"

    child_a = decoder.decode_stream(
        0x00121684,
        max_steps=256,
        max_bytes=512,
        follow_control_flow=True,
    )
    assert child_a["bytes_decoded"] == 92
    assert child_a["stopped_reason"] == "control_flow_cycle"

    child_b = decoder.decode_stream(
        0x001216AA,
        max_steps=256,
        max_bytes=512,
        follow_control_flow=True,
    )
    assert child_b["bytes_decoded"] == 54
    assert child_b["stopped_reason"] == "control_flow_cycle"


def test_type4d_type7b_response_child_movement_streams_are_exact():
    symbols = SymbolStore()
    expected = {
        0x00121710: (0x0012171B, 12, "ACTOR_MOVE_TYPE4D_TYPE12_RESPONSE_CHILD"),
        0x0012171C: (0x001217A1, 134, "ACTOR_MOVE_TYPE7B_RESPONSE_CHILD"),
    }
    for address, (end, size, name) in expected.items():
        stream = symbols.at(address, include_ranges=False)
        assert stream is not None
        assert stream.name == name
        assert stream.end == end
        assert stream.size == size
        assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = MovementDecoder(rom)
    for address, (end, size, _) in expected.items():
        linear = decoder.decode_stream(
            address,
            max_steps=256,
            max_bytes=size,
            follow_control_flow=False,
        )
        assert linear["bytes_decoded"] == size
        assert linear["stopped_reason"] == "byte_limit"
        assert linear["steps"][-1]["next_address"] == f"0x{end + 1:08X}"

        decoded = decoder.decode_stream(
            address,
            max_steps=256,
            max_bytes=512,
            follow_control_flow=True,
        )
        assert decoded["bytes_decoded"] == size
        assert decoded["stopped_reason"] == "control_flow_cycle"

    child = decoder.decode_stream(0x0012171C, 256, 512, True)
    jumps = [
        command
        for step in child["steps"]
        for command in step["commands"]
        if command["opcode"] == "0x80"
    ]
    assert jumps[-1]["branch_target"] == "0x00121788"


def test_scene_table_transition_movement_stream_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x001209C6, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_MOVE_TYPE84_SCENE_TABLE_TRANSITION"
    assert stream.end == 0x001209EF
    assert stream.size == 42
    assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = MovementDecoder(rom)
    linear = decoder.decode_stream(
        0x001209C6,
        max_steps=256,
        max_bytes=42,
        follow_control_flow=False,
    )
    assert linear["bytes_decoded"] == 42
    assert linear["stopped_reason"] == "byte_limit"
    assert linear["steps"][-1]["next_address"] == "0x001209F0"

    decoded = decoder.decode_stream(
        0x001209C6,
        max_steps=256,
        max_bytes=512,
        follow_control_flow=True,
    )
    assert decoded["stopped_reason"] == "control_flow_cycle"
    jump = next(
        command
        for step in decoded["steps"]
        for command in step["commands"]
        if command["opcode"] == "0x80"
    )
    assert jump["branch_target"] == "0x001209C6"


def test_shared_movement_root_family_is_exact():
    symbols = SymbolStore()
    expected = {
        0x001203D0: (0x001203D7, "ACTOR_MOVE_TYPE2F_LEVEL12_TERMINAL_EVENT"),
        0x001203D8: (0x001203DF, "ACTOR_MOVE_TYPE84_SHARED_STEP_2"),
        0x001203E0: (0x001203E7, "ACTOR_MOVE_TYPE84_SHARED_STEP_1"),
        0x001203F2: (0x001203F9, "ACTOR_MOVE_TYPE50_LEVEL09_SPAWN"),
        0x001203FA: (0x00120431, "ACTOR_MOVE_TYPE15_PROXIMITY_RESPONSE"),
    }
    for address, (end, name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        assert symbol.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = MovementDecoder(rom)
    for address in (0x001203D0, 0x001203D8, 0x001203E0, 0x001203F2):
        decoded = decoder.decode_stream(address, 256, 512, True)
        assert decoded["bytes_decoded"] == 8
        assert decoded["stopped_reason"] == "control_flow_cycle"

    type15 = decoder.decode_stream(0x001203FA, 256, 512, True)
    assert type15["bytes_decoded"] == 30
    assert type15["stopped_reason"] == "control_flow_cycle"
    assert symbols.at(0x00120418, include_ranges=False).metadata["entry_offset"] == 30
    assert symbols.at(0x00120428, include_ranges=False).metadata["entry_offset"] == 46
    assert symbols.at(0x001203E8, include_ranges=False) is None


def test_shared_type3c_3d_3e_3f_movement_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x0012146C, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_MOVE_SHARED_TYPE3C_3D_3E_3F_RESPONSE"
    assert stream.end == 0x001214B1
    assert stream.size == 70
    assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoded = MovementDecoder(rom).decode_stream(
        0x0012146C,
        max_steps=256,
        max_bytes=512,
        follow_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 70
    assert decoded["stopped_reason"] == "control_flow_cycle"


def test_type84_0f22_response_family_is_exact_with_template_reachability_provisional():
    symbols = SymbolStore()
    template = symbols.at(0x001B8304, include_ranges=False)
    assert template is not None
    assert template.name == "ACTOR_TEMPLATE_TYPE_84_0F22_WALL_RESPONSE"
    assert template.size == 20
    assert template.metadata["type"] == "actor_template"
    assert template.confidence == "provisional"

    movement = symbols.at(0x00121412, include_ranges=False)
    assert movement is not None
    assert movement.name == "ACTOR_MOVE_TYPE84_0F22_WALL_RESPONSE_PREFIX"
    assert movement.end == 0x0012146B
    assert movement.size == 90
    assert movement.metadata["type"] == "movement_stream"
    assert movement.confidence == "decompiled"

    animation = symbols.at(0x00125D58, include_ranges=False)
    assert animation is not None
    assert animation.name == "ACTOR_ANIM_TYPE84_0F22_WALL_RESPONSE_LOOP"
    assert animation.end == 0x00125D7D
    assert animation.size == 38
    assert animation.metadata["type"] == "animation_stream"
    assert animation.confidence == "decompiled"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    movement_decoder = MovementDecoder(rom)
    movement_decoded = movement_decoder.decode_stream(
        0x00121412,
        max_steps=256,
        max_bytes=90,
        follow_control_flow=False,
    )
    assert movement_decoded["bytes_decoded"] == 90
    assert movement_decoded["stopped_reason"] == "byte_limit"
    assert movement_decoded["steps"][-1]["next_address"] == "0x0012146C"

    animation_decoder = load_animation_decoder().AnimationDecoder(rom)
    animation_decoded = animation_decoder.decode_stream(
        0x00125D58,
        max_instructions=64,
        max_bytes=128,
        follow_control_flow=True,
    )
    assert animation_decoded["bytes_decoded"] == 38
    assert animation_decoded["stopped_reason"] == "control_flow_cycle"
    assert animation_decoded["instructions"][-1]["branch_target"] == "0x00125D58"


def test_unreferenced_actor_template_records_are_exact_and_provisional():
    symbols = SymbolStore()
    expected = {
        0x001B7990: ("ACTOR_TEMPLATE_TYPE_84_UNREFERENCED_RESOURCE10", 0x001B79A3),
        0x001B7A58: ("ACTOR_TEMPLATE_TYPE_84_UNREFERENCED_PAYLOAD_40001400", 0x001B7A6B),
        0x001B82DC: ("ACTOR_TEMPLATE_TYPE_84_UNREFERENCED_ANIM_124CE4", 0x001B82EF),
    }
    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    expected_bytes = {
        0x001B7990: "840008000000000000006000000000000A000000",
        0x001B7A58: "8401400014000000000060000000000000000000",
        0x001B82DC: "84050000000000000000000000124CE409000400",
    }
    for address, (name, end) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        assert symbol.size == 20
        assert symbol.metadata["type"] == "actor_template"
        assert symbol.confidence == "provisional"
        assert rom[address:end + 1] == bytes.fromhex(expected_bytes[address])


def test_type84_presentation_response_movement_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x001214B2, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_MOVE_TYPE84_PRESENTATION_RESPONSE"
    assert stream.end == 0x00121529
    assert stream.size == 120
    assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    decoder = MovementDecoder(load_animation_decoder().RomReader(rom_path.read_bytes()))
    decoded = decoder.decode_stream(
        0x001214B2,
        max_steps=256,
        max_bytes=120,
        follow_control_flow=False,
    )
    assert decoded["bytes_decoded"] == 120
    assert decoded["stopped_reason"] == "byte_limit"
    assert decoded["steps"][-1]["next_address"] == "0x0012152A"

    alternate = next(
        command
        for step in decoded["steps"]
        for command in step["commands"]
        if command["opcode"] == "0x88"
    )
    assert alternate["branch_target"] == "0x00121502"


def test_level08_exit_movement_stream_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x0012152A, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_MOVE_TYPE60_LEVEL08_EXIT_PRESENTATION"
    assert stream.end == 0x00121597
    assert stream.size == 110
    assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoded = MovementDecoder(rom).decode_stream(
        0x0012152A,
        max_steps=256,
        max_bytes=512,
        follow_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 110
    assert decoded["stopped_reason"] == "control_flow_cycle"


def test_type62_63_player_collision_movement_stream_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x00121598, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_MOVE_TYPE62_63_PLAYER_COLLISION_RESPONSE"
    assert stream.end == 0x001215D7
    assert stream.size == 64
    assert stream.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    decoder = MovementDecoder(load_animation_decoder().RomReader(rom_path.read_bytes()))
    decoded = decoder.decode_stream(
        0x00121598,
        max_steps=256,
        max_bytes=64,
        follow_control_flow=False,
    )
    assert decoded["bytes_decoded"] == 64
    assert decoded["stopped_reason"] == "byte_limit"
    assert decoded["steps"][-1]["next_address"] == "0x001215D8"


def test_type29_player_collision_response_streams_are_exact():
    symbols = SymbolStore()

    function = symbols.at(0x001AF400, include_ranges=False)
    assert function is not None
    assert function.name == "ActorType29_PlayerCollisionHandler"
    assert function.end == 0x001AF467
    assert function.size == 104

    movement = symbols.at(0x001215E0, include_ranges=False)
    assert movement is not None
    assert movement.name == "ACTOR_MOVE_TYPE29_PLAYER_COLLISION_RESPONSE"
    assert movement.end == 0x00121617
    assert movement.size == 56
    assert movement.metadata["type"] == "movement_stream"

    animation = symbols.at(0x00121C30, include_ranges=False)
    assert animation is not None
    assert animation.name == "ACTOR_ANIM_TYPE29_PLAYER_COLLISION_RESPONSE"
    assert animation.end == 0x00121C61
    assert animation.size == 50
    assert animation.metadata["type"] == "animation_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = rom_path.read_bytes()
    reader = load_animation_decoder().RomReader(rom)
    movement_decoder = MovementDecoder(reader)
    movement_decoded = movement_decoder.decode_stream(
        0x001215E0,
        max_steps=256,
        max_bytes=56,
        follow_control_flow=False,
    )
    assert movement_decoded["bytes_decoded"] == 56
    assert movement_decoded["stopped_reason"] == "byte_limit"
    assert movement_decoded["steps"][-1]["next_address"] == "0x00121618"

    animation_decoded = load_animation_decoder().AnimationDecoder(reader).decode_stream(
        0x00121C30,
        max_instructions=256,
        max_bytes=50,
        follow_control_flow=False,
    )
    assert animation_decoded["bytes_decoded"] == 50
    assert animation_decoded["stopped_reason"] == "unconditional_jump"
    assert animation_decoded["instructions"][-1]["branch_target"] == "0x00121C38"


def test_type3e_3f_player_collision_response_family_is_exact():
    symbols = SymbolStore()

    expected_functions = {
        0x001AF2B0: (0x001AF2F9, "ActorType3E_PlayerCollisionHandler"),
        0x001AF2FA: (0x001AF343, "ActorType3F_PlayerCollisionHandler"),
    }
    for address, (end, name) in expected_functions.items():
        function = symbols.at(address, include_ranges=False)
        assert function is not None
        assert function.name == name
        assert function.end == end
        assert function.size == end - address + 1

    movement = symbols.at(0x00121618, include_ranges=False)
    assert movement is not None
    assert movement.name == "ACTOR_MOVE_TYPE3E_3F_PLAYER_COLLISION_RESPONSE"
    assert movement.end == 0x00121683
    assert movement.size == 108
    assert movement.metadata["type"] == "movement_stream"

    template = symbols.at(0x001B7B5C, include_ranges=False)
    assert template is not None
    assert template.name == "ACTOR_TEMPLATE_TYPE_84_TYPE3E_3F_COLLISION_CHILD"
    assert template.end == 0x001B7B6F
    assert template.size == 20
    assert template.metadata["type"] == "actor_template"
    assert template.metadata["runtime_type"] == 0x84
    assert template.metadata["animation_stream"] == 0x00122F80
    assert template.metadata["resource_count"] == 0x01

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    decoder = MovementDecoder(load_animation_decoder().RomReader(rom_path.read_bytes()))
    decoded = decoder.decode_stream(
        0x00121618,
        max_steps=256,
        max_bytes=108,
        follow_control_flow=False,
    )
    assert decoded["bytes_decoded"] == 108
    assert decoded["stopped_reason"] == "byte_limit"
    assert decoded["steps"][-1]["next_address"] == "0x00121684"
    assert rom_path.read_bytes()[0x001B7B5C:0x001B7B70] == bytes.fromhex(
        "84004000000000000000600000122F8001000000"
    )


def test_extended_player_collision_handler_family_is_exact():
    symbols = SymbolStore()
    expected_functions = {
        0x001AF21E: (0x001AF227, "ActorType3B_PlayerCollisionHandler"),
        0x001AF264: (0x001AF2AF, "ActorType42_PlayerCollisionHandler"),
        0x001AF344: (0x001AF383, "ActorType37_3C_PlayerCollisionHandler"),
        0x001AF384: (0x001AF3C1, "ActorType3D_PlayerCollisionHandler"),
        0x001AF3C2: (0x001AF3FF, "ActorType41_PlayerCollisionHandler"),
        0x001AF4A0: (0x001AF4D7, "ActorType33_38_39_PlayerCollisionHandler"),
        0x001AF53E: (0x001AF549, "ActorType5A_PlayerCollisionHandler"),
        0x001AF54A: (0x001AF555, "ActorType5B_PlayerCollisionHandler"),
        0x001AF556: (0x001AF561, "ActorType5C_PlayerCollisionHandler"),
        0x001AF562: (0x001AF56B, "ActorType5D_PlayerCollisionHandler"),
        0x001AF590: (0x001AF5EF, "ActorType55_56_57_PlayerCollisionHandler"),
        0x001AF5F0: (0x001AF637, "ActorType58_PlayerCollisionHandler"),
        0x001AF638: (0x001AF6AB, "ActorType5E_PlayerCollisionHandler"),
        0x001AF6AC: (0x001AF6DB, "ActorType5F_PlayerCollisionHandler"),
        0x001AF6DC: (0x001AF73F, "ActorType60_61_PlayerCollisionHandler"),
        0x001AFD84: (0x001AFE1B, "ActorType01_PlayerCollisionHandler"),
        0x001ABF9C: (0x001ABFCF, "Actor_InstallType01CollisionResponse"),
        0x001ABFD0: (0x001ABFEF, "Actor_ReinitializeCollisionResponseAfterType2D"),
        0x001B7474: (0x001B7493, "InteractionSpawn_RuntimeType45_AdjacentVariant"),
        0x001B74A0: (0x001B74B1, "InteractionSpawn_RuntimeType5B_AdjacentVariant"),
        0x001AFE1C: (0x001AFF81, "ActorType7E_PlayerCollisionHandler"),
        0x001AFF82: (0x001AFFE3, "ActorType02_PlayerCollisionHandler"),
        0x001AC60E: (0x001AC613, "ActorType0D_ActorCollisionHandler"),
    }
    for address, (end, name) in expected_functions.items():
        function = symbols.at(address, include_ranges=False)
        assert function is not None
        assert function.name == name
        assert function.end == end
        assert function.size == end - address + 1

    rom_data = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom_data[0x001ABFD0:0x001ABFF0] == bytes.fromhex(
        "4211"
        "6100239E"
        "6100FEB2"
        "61002396"
        "2A4A"
        "4DF9001B7ABC"
        "61002322"
        "6100416A"
        "4E75"
    )


    expected_pointers = {
        0x001CC2: (0x01, 0x001AFD84),
        0x001CC6: (0x02, 0x001AFF82),
        0x001D8A: (0x33, 0x001AF4A0),
        0x001D9A: (0x37, 0x001AF344),
        0x001D9E: (0x38, 0x001AF4A0),
        0x001DA2: (0x39, 0x001AF4A0),
        0x001DAA: (0x3B, 0x001AF21E),
        0x001DAE: (0x3C, 0x001AF344),
        0x001DB2: (0x3D, 0x001AF384),
        0x001DC2: (0x41, 0x001AF3C2),
        0x001DC6: (0x42, 0x001AF264),
        0x001E12: (0x55, 0x001AF590),
        0x001E16: (0x56, 0x001AF590),
        0x001E1A: (0x57, 0x001AF590),
        0x001E1E: (0x58, 0x001AF5F0),
        0x001E26: (0x5A, 0x001AF53E),
        0x001E2A: (0x5B, 0x001AF54A),
        0x001E2E: (0x5C, 0x001AF556),
        0x001E32: (0x5D, 0x001AF562),
        0x001E36: (0x5E, 0x001AF638),
        0x001E3A: (0x5F, 0x001AF6AC),
        0x001E3E: (0x60, 0x001AF6DC),
        0x001E42: (0x61, 0x001AF6DC),
        0x001EB6: (0x7E, 0x001AFE1C),
        0x001EEE: (0x0D, 0x001AC60E),
    }
    rom = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    data = rom.read_bytes()
    for address, (actor_type, target) in expected_pointers.items():
        pointer = symbols.at(address, include_ranges=False)
        assert pointer is not None
        assert pointer.metadata["type"] == "rom_pointer"
        assert int.from_bytes(data[address:address + 4], "big") == target
        table_base = 0x001CBE if address <= 0x001EB9 else 0x001EBA
        assert address == table_base + actor_type * 4

    action_response = symbols.at(0x00FFF0D8, include_ranges=False)
    assert action_response is not None
    assert action_response.name == "PLAYER_ACTION_RESPONSE_FIELD"


def test_unindexed_interaction_spawn_variant_is_exact():
    symbols = SymbolStore()
    function = symbols.at(0x001B6BFC, include_ranges=False)
    assert function is not None
    assert function.name == "InteractionSpawn_RuntimeType57_AdjacentVariant"
    assert function.end == 0x001B6C0D
    assert function.size == 0x12

    rom = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    assert rom.read_bytes()[0x001B6BFC:0x001B6C0E] == bytes.fromhex(
        "4DF9001B7A1C"
        "6100E65A"
        "6604"
        "1ABC0057"
        "4E75"
    )


def test_adjacent_interaction_spawn_variants_have_exact_bodies():
    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001B7474:0x001B7494] == bytes.fromhex(
        "4DF9001B79B86100DDEA6612"
        "1ABC00452B7C00122C400020"
        "1B7C000100294E75"
    )
    assert rom[0x001B74A0:0x001B74B2] == bytes.fromhex(
        "4DF9001B7A086100DDBE6604"
        "1ABC005B4E75"
    )


def test_interaction_anchor_callback_family_is_exact():
    symbols = SymbolStore()
    expected = {
        0x001B57C4: (0x001B584F, "Actor_ApplyInteractionMarkerMovementStep"),
        0x001B5850: (0x001B58B9, "Actor_ApplyInteractionAnchorMovementStep"),
        0x001B58BA: (0x001B58D7, "Actor_SetInteractionAnchorFromActor"),
    }
    owners = []
    for address, (end, name) in expected.items():
        function = symbols.at(address, include_ranges=False)
        assert function is not None
        assert function.name == name
        assert function.end == end
        assert function.size == end - address + 1
        owners.append((function.address, function.end))
    assert all(right < next_left for (_, right), (next_left, _) in zip(owners, owners[1:]))

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = rom_path.read_bytes()
    for address, (end, _) in expected.items():
        assert rom[end - 1:end + 1] == bytes.fromhex("4E75")

    assert rom[0x001B58BA:0x001B58D8] == bytes.fromhex(
        "3E2900020447002033C700FFF0943E2900040647004033C700FFF0964E75"
    )


def test_level_event_movement_stream_family_is_exact():
    symbols = SymbolStore()
    expected = {
        0x00120FB4: (0x00120FDD, "ACTOR_MOVE_TYPE84_LEVEL_EVENT_VARIANT_B"),
        0x00120FDE: (0x00120FFD, "ACTOR_MOVE_TYPE17_INTERACTION"),
        0x00120FFE: (0x00121033, "ACTOR_MOVE_TYPE46_LEVEL_EVENT_MODE_READY"),
        0x00121082: (0x001210FD, "ACTOR_MOVE_TYPE2F_LEVEL_EVENT_MOVING_VARIANT"),
    }
    for address, (end, name) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        assert symbol.size == end - address + 1
        assert symbol.metadata["type"] == "movement_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = MovementDecoder(rom)
    for address, (end, _) in expected.items():
        decoded = decoder.decode_stream(
            address,
            max_steps=256,
            max_bytes=512,
            follow_control_flow=True,
        )
        assert decoded["bytes_decoded"] == end - address + 1
        assert decoded["stopped_reason"] == "control_flow_cycle"


def test_player_response_reset_fallback_animation_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x00121C28, include_ranges=False)
    assert stream is not None
    assert stream.name == "PLAYER_ANIM_RESPONSE_RESET_FALLBACK"
    assert stream.end == 0x00121C2F
    assert stream.size == 8
    assert stream.metadata["type"] == "animation_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoded = load_animation_decoder().AnimationDecoder(rom).decode_stream(
        0x00121C28,
        max_instructions=16,
        max_bytes=64,
        follow_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 8
    assert decoded["stopped_reason"] == "control_flow_cycle"
    assert decoded["instructions"][0]["resolved_frame"] == "0x001E5D0C"
    assert decoded["instructions"][1]["branch_target"] == "0x00121C28"


def test_actor_type4d_collision_response_animation_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x001222C2, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_ANIM_TYPE12_TYPE4D_RESPONSE"
    assert stream.end == 0x001222D1
    assert stream.size == 16
    assert stream.metadata["type"] == "animation_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoded = load_animation_decoder().AnimationDecoder(rom).decode_stream(
        0x001222C2,
        max_instructions=16,
        max_bytes=64,
        follow_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 16
    assert decoded["stopped_reason"] == "dynamic_state_selection"
    assert decoded["instructions"][-1]["opcode"] == "0xF8"


def test_player_animation_lookup_family_is_exact():
    symbols = SymbolStore()
    vertical = (
        (0x00121868, 0x00121875, "PLAYER_ANIM_VERTICAL_BAND_07_15", "0x07BA"),
        (0x00121876, 0x00121883, "PLAYER_ANIM_VERTICAL_BAND_06_14", "0x07BE"),
        (0x00121884, 0x00121891, "PLAYER_ANIM_VERTICAL_BAND_05_13", "0x07C2"),
        (0x00121892, 0x0012189F, "PLAYER_ANIM_VERTICAL_BAND_04_12", "0x07C6"),
        (0x001218A0, 0x001218AD, "PLAYER_ANIM_VERTICAL_BAND_03_11", "0x07CA"),
        (0x001218AE, 0x001218BB, "PLAYER_ANIM_VERTICAL_BAND_02_10", "0x07CE"),
        (0x001218BC, 0x001218C9, "PLAYER_ANIM_VERTICAL_BAND_01_09", "0x07D2"),
        (0x001218CA, 0x001218D7, "PLAYER_ANIM_VERTICAL_BAND_00_08", "0x07D6"),
    )
    interaction = (
        (0x00121900, 0x00121909, "PLAYER_ANIM_INTERACTION_VARIANT_00", "0x08CE"),
        (0x0012190A, 0x00121913, "PLAYER_ANIM_INTERACTION_VARIANT_01", "0x08D2"),
        (0x00121914, 0x0012191D, "PLAYER_ANIM_INTERACTION_VARIANT_02", "0x08D6"),
        (0x0012191E, 0x00121927, "PLAYER_ANIM_INTERACTION_VARIANT_03", "0x08DA"),
        (0x00121928, 0x00121931, "PLAYER_ANIM_INTERACTION_VARIANT_04", "0x08DE"),
        (0x00121932, 0x0012193B, "PLAYER_ANIM_INTERACTION_VARIANT_05", "0x08E2"),
        (0x0012193C, 0x00121945, "PLAYER_ANIM_INTERACTION_VARIANT_06", "0x08E6"),
        (0x00121946, 0x0012194F, "PLAYER_ANIM_INTERACTION_VARIANT_07", "0x08EA"),
        (0x00121950, 0x00121959, "PLAYER_ANIM_INTERACTION_VARIANT_08", "0x08EE"),
        (0x0012195A, 0x00121963, "PLAYER_ANIM_INTERACTION_VARIANT_09", "0x08F2"),
    )
    for address, end, name, frame in vertical + interaction:
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        assert symbol.size == end - address + 1
        assert symbol.metadata["type"] == "animation_stream"

    vertical_table = symbols.at(0x00121828, include_ranges=False)
    assert vertical_table is not None
    assert vertical_table.size == 64
    assert vertical_table.end == 0x00121867
    assert vertical_table.metadata["count"] == 16

    interaction_table = symbols.at(0x001218D8, include_ranges=False)
    assert interaction_table is not None
    assert interaction_table.name == "PLAYER_INTERACTION_ANIMATION_TABLE"
    assert interaction_table.size == 40
    assert interaction_table.end == 0x001218FF
    assert interaction_table.metadata["count"] == 10

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = load_animation_decoder().AnimationDecoder(rom)
    for address, end, _, frame in vertical:
        decoded = decoder.decode_stream(address, 16, 64, True)
        assert decoded["bytes_decoded"] == end - address + 1
        assert decoded["stopped_reason"] == "dynamic_state_selection"
        assert decoded["instructions"][0]["reference"] == frame
    for address, end, _, frame in interaction:
        decoded = decoder.decode_stream(address, 16, 64, False)
        assert decoded["bytes_decoded"] == end - address + 1
        assert decoded["stopped_reason"] == "unconditional_jump"
        assert decoded["instructions"][0]["reference"] == frame
        assert decoded["instructions"][-1]["branch_target"] == "0x00121964"


def test_player_terrain_stop_alignment_animation_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x0012181A, include_ranges=False)
    assert stream is not None
    assert stream.name == "PLAYER_ANIM_TERRAIN_STOP_ALIGNMENT"
    assert stream.end == 0x00121827
    assert stream.size == 14
    assert stream.metadata["type"] == "animation_stream"

    vertical_table = symbols.at(0x00121828, include_ranges=False)
    assert vertical_table is not None
    assert vertical_table.name == "PLAYER_VERTICAL_ANIMATION_TABLE"
    assert stream.end + 1 == vertical_table.address

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = load_animation_decoder().AnimationDecoder(rom)
    decoded = decoder.decode_stream(
        0x0012181A,
        max_instructions=16,
        max_bytes=32,
        follow_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 14
    assert decoded["stopped_reason"] == "dynamic_state_selection"
    assert decoded["instructions"][0]["reference"] == "0x07BA"
    assert decoded["instructions"][1]["branch_target"] == "0x0012181A"
    assert decoded["instructions"][-1]["opcode"] == "0xF8"


def test_player_animation_branch_continuations_are_exact():
    symbols = SymbolStore()
    cases = (
        (0x00121AC8, 0x00121AC9, "PLAYER_ANIM_TERRAIN_STATE_SELECTOR", 2, "0xF8"),
        (0x00121F3A, 0x00121F69, "PLAYER_ANIM_IDLE_RANDOM_VARIANT", 48, "0xEA"),
        (0x00122128, 0x0012214D, "PLAYER_ANIM_SPECIAL_CAMERA_JUMP_CONTINUATION", 38, "0xF8"),
        (0x0012219E, 0x001221AF, "PLAYER_ANIM_TERRAIN_RESPONSE_JUMP_TAIL", 18, "0xF8"),
        (0x001221E8, 0x0012222D, "PLAYER_ANIM_TERRAIN_LAUNCH_RESPONSE_CONTINUATION", 70, "0xF8"),
    )
    for address, end, name, size, terminal_opcode in cases:
        stream = symbols.at(address, include_ranges=False)
        assert stream is not None
        assert stream.name == name
        assert stream.end == end
        assert stream.size == size
        assert stream.metadata["type"] == "animation_stream"

        rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
        rom = load_animation_decoder().RomReader(rom_path.read_bytes())
        decoded = load_animation_decoder().AnimationDecoder(rom).decode_stream(
            address,
            max_instructions=256,
            max_bytes=size,
            follow_control_flow=False,
            continue_after_control_flow=True,
        )
        assert decoded["bytes_decoded"] == size
        assert decoded["instructions"][-1]["opcode"] == terminal_opcode


def test_player_surface_recovery_animation_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x001223D0, include_ranges=False)
    assert stream is not None
    assert stream.name == "PLAYER_ANIM_SURFACE_RECOVERY"
    assert stream.end == 0x001223D9
    assert stream.size == 10
    assert stream.metadata["type"] == "animation_stream"

    apple_throw = symbols.at(0x001223DA, include_ranges=False)
    assert apple_throw is not None
    assert apple_throw.name == "PLAYER_ANIM_THROW_APPLE"
    assert stream.end + 1 == apple_throw.address

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoded = load_animation_decoder().AnimationDecoder(rom).decode_stream(
        0x001223D0,
        max_instructions=16,
        max_bytes=16,
        follow_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 10
    assert decoded["stopped_reason"] == "dynamic_state_selection"
    assert decoded["instructions"][0]["reference"] == "0x0B16"
    assert decoded["instructions"][1]["target_fields"] == ["0x01", "0xF0", "0xE7"]
    assert decoded["instructions"][-1]["opcode"] == "0xF8"


def test_player_action_continuation_banks_are_exact():
    symbols = SymbolStore()
    cases = (
        (0x001224BA, 0x00122503, "PLAYER_ANIM_ACTION_TERRAIN_TRANSITION_CONTINUATION", 74, "0xF8"),
        (0x0012257C, 0x001225A1, "PLAYER_ANIM_ACTION_TRANSITION_LOCK_CONTINUATION", 38, "0xF8"),
        (0x0012280C, 0x0012289F, "PLAYER_ANIM_ACTION_TERRAIN_PUSH_DOWN_CONTINUATION", 148, "0xF8"),
        (0x001229C2, 0x00122A0F, "PLAYER_ANIM_ACTION_AIRBORNE_CONTINUATION", 78, "0xEA"),
    )
    for address, end, name, size, terminal_opcode in cases:
        stream = symbols.at(address, include_ranges=False)
        assert stream is not None
        assert stream.name == name
        assert stream.end == end
        assert stream.size == size
        assert stream.metadata["type"] == "animation_stream"

        rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
        rom = load_animation_decoder().RomReader(rom_path.read_bytes())
        decoded = load_animation_decoder().AnimationDecoder(rom).decode_stream(
            address,
            max_instructions=512,
            max_bytes=size,
            follow_control_flow=False,
            continue_after_control_flow=True,
        )
        assert decoded["bytes_decoded"] == size
        assert decoded["instructions"][-1]["opcode"] == terminal_opcode


def test_type1e_proximity_movement_handoff_animation_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x001235E2, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_ANIM_TYPE1E_PROXIMITY_MOVEMENT_HANDOFF"
    assert stream.end == 0x00123613
    assert stream.size == 50
    assert stream.metadata["type"] == "animation_stream"

    loop = symbols.at(0x001235EC, include_ranges=False)
    assert loop is not None
    assert loop.name == "ACTOR_ANIM_TYPE1E_PROXIMITY_MOVEMENT_LOOP"
    assert loop.metadata["alias_of"] == "ACTOR_ANIM_TYPE1E_PROXIMITY_MOVEMENT_HANDOFF"
    assert loop.metadata["entry_offset"] == 10

    following = symbols.at(0x00123614, include_ranges=False)
    assert following is not None
    assert following.name == "ACTOR_ANIM_TYPE1E_PROXIMITY_RESPONSE"
    assert stream.end + 1 == following.address

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = load_animation_decoder().AnimationDecoder(rom)
    decoded = decoder.decode_stream(
        0x001235E2,
        max_instructions=64,
        max_bytes=50,
        follow_control_flow=False,
        continue_after_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 50
    assert decoded["stopped_reason"] == "byte_limit"
    assert decoded["instructions"][1]["value"] == "001204DA"
    assert decoded["instructions"][-1]["branch_target"] == "0x001235EC"


def test_type7a_interaction_roots_include_terminal_jumps():
    symbols = SymbolStore()
    cases = (
        (0x00125A68, 0x00125A87, "ACTOR_ANIM_TYPE7A_INTERACTION_06", 32, "0x00125A70"),
        (0x00125A88, 0x00125AA7, "ACTOR_ANIM_TYPE7A_INTERACTION_07", 32, "0x00125A90"),
        (0x00125AA8, 0x00125AC7, "ACTOR_ANIM_TYPE7A_INTERACTION_08", 32, "0x00125AB0"),
    )
    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = load_animation_decoder().AnimationDecoder(rom)
    for address, end, name, size, target in cases:
        stream = symbols.at(address, include_ranges=False)
        assert stream is not None
        assert stream.name == name
        assert stream.end == end
        assert stream.size == size
        assert stream.metadata["type"] == "animation_stream"

        decoded = decoder.decode_stream(
            address,
            max_instructions=64,
            max_bytes=size,
            follow_control_flow=False,
            continue_after_control_flow=True,
        )
        assert decoded["bytes_decoded"] == size
        assert decoded["stopped_reason"] == "byte_limit"
        assert decoded["instructions"][-1]["opcode"] == "0xEA"
        assert decoded["instructions"][-1]["branch_target"] == target


def test_type84_type0f_child_movement_response_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x00120A00, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_MOVE_TYPE84_TYPE0F_CHILD_RESPONSE"
    assert stream.end == 0x00120A41
    assert stream.size == 66
    assert stream.metadata["type"] == "movement_stream"

    following = symbols.at(0x00120A42, include_ranges=False)
    assert following is not None
    assert following.name == "ACTOR_MOVE_TYPE75_LEVEL_EXIT"
    assert stream.end + 1 == following.address

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoded = MovementDecoder(rom).decode_stream(
        0x00120A00,
        max_steps=128,
        max_bytes=66,
        follow_control_flow=False,
        continue_after_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 66
    assert decoded["stopped_reason"] == "byte_limit"
    assert decoded["steps"][0]["delta_x"] == -1
    assert decoded["steps"][0]["delta_y"] == 0
    assert decoded["steps"][-1]["commands"][-1]["opcode"] == "0x82"
    assert decoded["steps"][-1]["commands"][-1]["address"] == "0x00120A40"


def test_actor_type41_interaction_response_animation_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x00125D7E, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_ANIM_TYPE41_INTERACTION_RESPONSE"
    assert stream.end == 0x00125DC3
    assert stream.size == 70
    assert stream.metadata["type"] == "animation_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoded = load_animation_decoder().AnimationDecoder(rom).decode_stream(
        0x00125D7E,
        max_instructions=64,
        max_bytes=128,
        follow_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 70
    assert decoded["stopped_reason"] == "control_flow_cycle"
    assert decoded["instructions"][1]["opcode"] == "0xF5"
    assert decoded["instructions"][5]["opcode"] == "0xF5"
    assert decoded["instructions"][-1]["branch_target"] == "0x00125D90"


def test_type84_interaction_fd_fe_animation_family_is_exact():
    symbols = SymbolStore()
    expected = {
        0x00125DEA: (0x00125E07, 30, "ACTOR_ANIM_TYPE84_INTERACTION_FD_FE_ROOT"),
        0x00125E08: (0x00125E3F, 56, "ACTOR_ANIM_TYPE84_INTERACTION_FD_FE_VARIANT_A"),
        0x00125E40: (0x00125E71, 50, "ACTOR_ANIM_TYPE84_INTERACTION_FD_FE_VARIANT_B"),
    }
    for address, (end, size, name) in expected.items():
        stream = symbols.at(address, include_ranges=False)
        assert stream is not None
        assert stream.name == name
        assert stream.end == end
        assert stream.size == size
        assert stream.metadata["type"] == "animation_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = load_animation_decoder().AnimationDecoder(rom)
    for address, (end, size, _) in expected.items():
        decoded = decoder.decode_stream(
            address,
            max_instructions=128,
            max_bytes=size,
            follow_control_flow=False,
        )
        assert decoded["bytes_decoded"] == size
        assert decoded["stopped_reason"] == "unconditional_jump"
        assert decoded["instructions"][-1].get("branch_target") == "0x00125DEA"

    root = decoder.decode_stream(
        0x00125DEA,
        max_instructions=128,
        max_bytes=128,
        follow_control_flow=True,
    )
    assert root["bytes_decoded"] == 30
    assert root["stopped_reason"] == "control_flow_cycle"
    root_branch = next(
        instruction
        for instruction in root["instructions"]
        if instruction.get("opcode") == "0xFD"
    )
    assert root_branch["branch_target"] == "0x00125E08"

    variant_a = decoder.decode_stream(
        0x00125E08,
        max_instructions=128,
        max_bytes=128,
        follow_control_flow=True,
    )
    assert variant_a["bytes_decoded"] == 86
    assert variant_a["stopped_reason"] == "control_flow_cycle"
    random_branch = variant_a["instructions"][0]
    assert random_branch["branch_target"] == "0x00125E40"


def test_player_airborne_action_continuation_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x00122672, include_ranges=False)
    assert stream is not None
    assert stream.name == "PLAYER_ANIM_ACTION_AIRBORNE_RESPONSE_CONTINUATION"
    assert stream.end == 0x001226B1
    assert stream.size == 64
    assert stream.confidence == "decompiled"
    assert stream.metadata["type"] == "animation_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = load_animation_decoder().AnimationDecoder(rom)
    decoded = decoder.decode_stream(
        0x00122672,
        max_instructions=128,
        max_bytes=64,
        follow_control_flow=False,
    )
    assert decoded["bytes_decoded"] == 64
    assert decoded["stopped_reason"] == "unconditional_jump"
    assert rom.slice(0x00122672, 64).hex().upper() == (
        "F421F0C800000012269A0C56ED01F0DA0001F503001B791828DC0000000000000000"
        "FB00001B0360ED01F11F00010C5AED01F0DA00010C5E0C5EEA000012217A"
    )
    f5 = next(instruction for instruction in decoded["instructions"] if instruction.get("opcode") == "0xF5")
    assert f5["raw"] == "F503001B791828DC0000000000000000"
    callback = next(instruction for instruction in decoded["instructions"] if instruction.get("opcode") == "0xFB")
    assert callback["parameter"] == "0x001B0360"
    assert decoded["instructions"][-1]["branch_target"] == "0x0012217A"


def test_type84_menu_presentation_child_a_animation_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x00125F5A, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_ANIM_TYPE84_MENU_PRESENTATION_CHILD_A"
    assert stream.end == 0x0012602F
    assert stream.size == 214
    assert stream.metadata["type"] == "animation_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = load_animation_decoder().AnimationDecoder(rom)
    decoded = decoder.decode_stream(
        0x00125F5A,
        max_instructions=256,
        max_bytes=214,
        follow_control_flow=False,
    )
    assert decoded["bytes_decoded"] == 214
    assert decoded["stopped_reason"] == "byte_limit"
    assert decoded["instructions"][-1]["address"] == "0x0012602E"
    assert decoded["instructions"][-1]["opcode"] == "0xEC"

    f5_sites = [
        instruction["address"]
        for instruction in decoded["instructions"]
        if instruction.get("opcode") == "0xF5"
    ]
    assert f5_sites == [
        "0x00125F66",
        "0x00125F76",
        "0x00125F86",
        "0x00125F96",
        "0x00125FAA",
        "0x00125FBA",
        "0x00125FCA",
        "0x00125FDA",
        "0x00125FEA",
        "0x00125FFC",
        "0x0012600C",
        "0x0012601C",
    ]


def test_type64_interaction_animation_entry_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x00124CD8, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_ANIM_TYPE64_INTERACTION"
    assert stream.end == 0x00124CDB
    assert stream.size == 4
    assert stream.metadata["type"] == "animation_stream"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = load_animation_decoder().AnimationDecoder(rom)
    decoded = decoder.decode_stream(
        0x00124CD8,
        max_instructions=8,
        max_bytes=4,
        follow_control_flow=False,
    )
    assert decoded["bytes_decoded"] == 4
    assert decoded["stopped_reason"] == "byte_limit"
    assert decoded["instructions"][0]["reference"] == "0x1696"
    assert decoded["instructions"][1]["opcode"] == "0xEC"


def test_scene_reset_secondary_animation_and_embedded_entry_are_exact():
    symbols = SymbolStore()
    root = symbols.at(0x00125EEE, include_ranges=False)
    assert root is not None
    assert root.name == "ACTOR_ANIM_SCENE_RESET_SECONDARY"
    assert root.end == 0x00125F59
    assert root.size == 108
    assert root.metadata["type"] == "animation_stream"

    entry = symbols.at(0x00125F18, include_ranges=False)
    assert entry is not None
    assert entry.name == "ACTOR_ANIM_SCENE_RESET_SECONDARY_ENTRY"
    assert entry.metadata["alias_of"] == "ACTOR_ANIM_SCENE_RESET_SECONDARY"
    assert entry.metadata["entry_offset"] == 42

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoder = load_animation_decoder().AnimationDecoder(rom)
    decoded = decoder.decode_stream(
        0x00125EEE,
        max_instructions=128,
        max_bytes=256,
        follow_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 108
    assert decoded["stopped_reason"] == "control_flow_cycle"
    assert decoded["instructions"][20]["opcode"] == "0xEC"
    assert decoded["instructions"][-1]["branch_target"] == "0x00125F24"

    embedded = decoder.decode_stream(
        0x00125F18,
        max_instructions=64,
        max_bytes=128,
        follow_control_flow=True,
    )
    assert embedded["bytes_decoded"] == 66
    assert embedded["stopped_reason"] == "control_flow_cycle"
    assert embedded["instructions"][-1]["branch_target"] == "0x00125F24"


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


def test_upper_type1f_type22_collision_animation_family_is_exact():
    symbols = SymbolStore()
    expected = {
        0x0012384A: (0x00123879, "ACTOR_ANIM_TYPE1F_ACTOR_COLLISION_RESPONSE"),
        0x0012387A: (0x001238A9, "ACTOR_ANIM_TYPE22_ACTOR_COLLISION_RESPONSE"),
        0x001238AA: (0x001238F7, "ACTOR_ANIM_TYPE22_PLAYER_COLLISION_RESPONSE"),
        0x001238F8: (0x001238FF, "ACTOR_ANIM_TYPE1F_COLLISION_RESPONSE_ALTERNATE_PREFIX"),
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
        0x001238B2: ("ACTOR_ANIM_TYPE22_PROXIMITY_LOOP", 8),
        0x001238C4: ("ACTOR_ANIM_TYPE22_RESPONSE_SEQUENCE", 26),
    }
    for address, (name, offset) in aliases.items():
        alias = symbols.at(address, include_ranges=False)
        assert alias is not None
        assert alias.name == name
        assert alias.metadata["alias_of"] == "ACTOR_ANIM_TYPE22_PLAYER_COLLISION_RESPONSE"
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


def test_canonical_scene_rnc_graphics_have_exact_loader_extents():
    symbols = SymbolStore()
    expected = (
        (0x0012B7A4, 0x0012BE16, "LEVEL08_EXIT_C000_GRAPHICS"),
        (0x0012CCD8, 0x0012CD72, "SCENE_SETUP_C000_GRAPHICS"),
        (0x0012CD74, 0x0012CE04, "SCENE_SETUP_E000_GRAPHICS"),
        (0x0012CE06, 0x0012D0F8, "SCENE_SETUP_SECONDARY_E000_GRAPHICS"),
        (0x0012D0FA, 0x0012D653, "SCENE_SHARED_GRAPHICS_RESOURCE"),
        (0x0012D870, 0x0012DA03, "SCENE_STATE07_C000_GRAPHICS"),
        (0x0012DD76, 0x0012DF6A, "SCENE_SHARED_C000_GRAPHICS"),
        (0x0012DF6C, 0x0012E175, "SCENE_STATE0B_C000_GRAPHICS"),
        (0x0012E4BE, 0x0012E665, "SCENE_STATE00_E000_GRAPHICS"),
        (0x0012E7EA, 0x0012EA10, "SCENE_COMMON_E000_GRAPHICS"),
        (0x0012EA12, 0x0012F12C, "SCENE_TRANSITION_E000_GRAPHICS"),
        (0x0012F12E, 0x0012F39D, "SCENE_TRANSITION_C000_GRAPHICS"),
        (0x0012F4EC, 0x0012F711, "SCENE_COMMON_C000_GRAPHICS"),
        (0x00132F8E, 0x00136910, "SCENE_COMMON_BASE_GRAPHICS"),
        (0x00136912, 0x0013A891, "SCENE_SHARED_BASE_GRAPHICS"),
    )
    actual = []
    for start, end, name in expected:
        symbol = symbols.at(start, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.size == end - start + 1
        assert symbol.end == end
        assert symbol.metadata["type"] == "graphics_data"
        assert not symbol.is_mechanical
        actual.append((symbol.address, symbol.end))
    assert actual == [(start, end) for start, end, _ in expected]
    assert all(left[1] < right[0] for left, right in zip(actual, actual[1:]))


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


def test_static_header_text_and_shared_palette_families_are_exact():
    symbols = SymbolStore()
    expected = {
        0x00000100: (0x000001A3, "ROM_SEGA_HEADER_FIELDS", "rom_header"),
        0x0012659C: (0x00126678, "SCENE_TRANSITION_LEVEL_NAME_TABLE", "text_data"),
        0x00126D4E: (0x00126D7D, "ROM_FIXED_WIDTH_BLANK_RECORDS_126D4E", "text_data"),
        0x00126EB6: (0x00126EBF, "ROM_ASCII_NUMERIC_LABELS_126EB6", "text_data"),
        0x00126EC0: (0x00126F0D, "LEVEL_RESULT_MESSAGE_TABLE", "text_data"),
        0x0012755A: (0x00127570, "SCENE_RESOURCE_PRINCESS_RESPONSE_TEXT", "text_data"),
        0x00127E80: (0x00127E8B, "BONUS_LEVEL_LABEL_STREAM", "text_data"),
        0x00128E4F: (0x00128E5A, "MENU_PRESENTS_LABEL_STREAM", "text_data"),
        0x00129AD2: (0x00129B51, "SCENE_DISPATCH_PALETTE_SOURCE", "palette_data"),
        0x00121034: (0x00121081, "ACTOR_MOVE_TYPE40_TYPE3A_LEVEL_EVENT_PRELUDE", "movement_stream"),
    }
    for address, (end, name, symbol_type) in expected.items():
        symbol = symbols.at(address, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        assert symbol.size == end - address + 1
        assert symbol.metadata["type"] == symbol_type

    numeric_labels = symbols.at(0x00126EB6, include_ranges=False)
    assert numeric_labels is not None
    assert numeric_labels.confidence == "provisional"

    blank_records = symbols.at(0x00126D4E, include_ranges=False)
    assert blank_records is not None
    assert blank_records.metadata["entry_size"] == 0x10
    assert blank_records.metadata["count"] == 3
    assert blank_records.confidence == "provisional"

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x00000100:0x00000110] == b"SEGA GENESIS    "
    assert rom[0x0012659C:0x00126679].count(b"\0") == 13
    assert rom[0x00126D4E:0x00126D7E] == (b"\xff" + b" " * 14 + b"\0") * 3
    assert rom[0x00126EB6:0x00126EC0] == b"30\00060\00090\000\000"
    assert rom[0x00126EC0:0x00126F0E].count(b"\0") == 6
    assert rom[0x0012755A:0x00127571].endswith(b"WITH A PRINCESS!\0")
    assert rom[0x00127E80:0x00127E8C] == b"BONUS LEVEL\0"
    assert rom[0x00128E4F:0x00128E5B].endswith(b"PRESENTS\0")
    assert rom[0x00129AD2:0x00129ADA] == bytes.fromhex("0006000000020024")
    assert rom[0x00129B4E:0x00129B52] == bytes.fromhex("0e ee 00 00")

    decoder = MovementDecoder(load_animation_decoder().RomReader(rom))
    decoded = decoder.decode_stream(
        0x00121034,
        max_steps=128,
        max_bytes=0x4E,
        follow_control_flow=True,
    )
    assert decoded["bytes_decoded"] == 0x4E
    assert decoded["stopped_reason"] == "byte_limit"
    assert decoded["steps"][-1]["next_address"] == "0x00121082"


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


def test_canonical_level_event_streams_have_exact_records_and_terminators():
    symbols = SymbolStore()
    expected = (
        (0x00002128, 0x000024FB, 0x000024FA, 163, "LEVEL_EVENT_STREAM_LEVEL02"),
        (0x000024FC, 0x0000262E, 0x0000262E, 51, "LEVEL_EVENT_STREAM_LEVEL06"),
    )
    for start, end, terminator, count, name in expected:
        symbol = symbols.at(start, include_ranges=False)
        assert symbol is not None
        assert symbol.name == name
        assert symbol.end == end
        assert symbol.size == end - start + 1
        assert symbol.metadata["type"] == "level_event_stream"
        assert symbol.metadata["record_size"] == 6
        assert symbol.metadata["count"] == count
        assert symbol.metadata["terminator"] == terminator

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = rom_path.read_bytes()
    for start, end, terminator, count, _ in expected:
        assert terminator - start == count * 6
        assert all(rom[start + offset] != 0 for offset in range(0, terminator - start, 6))
        assert rom[terminator] == 0
        assert end >= terminator
    assert rom[0x24FC:0x24FC + 6] == bytes.fromhex("B4ED00620100")
    assert rom[0x262F:0x2631] == bytes.fromhex("FE60")


def test_canonical_level08_event_command_stream_has_exact_pairs_and_boundaries():
    symbols = SymbolStore()
    stream = symbols.at(0x0000262F, include_ranges=False)
    assert stream is not None
    assert stream.name == "LEVEL08_EXIT_EVENT_COMMAND_STREAM"
    assert stream.end == 0x000029A4
    assert stream.size == 0x376
    assert stream.metadata["type"] == "level_event_stream"
    assert stream.metadata["record_size"] == 2
    assert stream.metadata["count"] == 443

    padding = symbols.at(0x000029A5, include_ranges=False)
    assert padding is not None
    assert padding.name == "LEVEL08_EVENT_STREAM_ALIGNMENT_PADDING"
    assert padding.size == 1
    assert padding.confidence == "confirmed"

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = rom_path.read_bytes()
    assert rom[0x1B64D8:0x1B64E2] == bytes.fromhex("23FC0000262F00FFF12E")
    records = rom[0x262F:0x29A5]
    assert len(records) == 0x376
    assert len(records) % 2 == 0
    opcodes = records[::2]
    assert len(opcodes) == 443
    assert all(0xF2 <= opcode <= 0xFF for opcode in opcodes)
    assert records[-2:] == bytes.fromhex("F201")
    assert rom[0x29A5] == 0
    assert rom[0x29A6:0x29A6 + 4] == bytes.fromhex("E6AEE6AE")


def test_canonical_actor_surface_flags_table_has_byte_indexed_extent():
    symbols = SymbolStore()
    table = symbols.at(0x0000683E, include_ranges=False)
    assert table is not None
    assert table.name == "TERRAIN_ACTOR_SURFACE_FLAGS_TABLE"
    assert table.end == 0x0000693D
    assert table.size == 0x100
    assert table.metadata["type"] == "terrain_actor_surface_flags_table"
    assert table.metadata["entry_size"] == 1
    assert table.metadata["count"] == 256

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = rom_path.read_bytes()
    assert len(rom[0x683E:0x693E]) == 0x100
    assert rom[0x683E:0x684E] == bytes(0x10)
    assert rom[0x693E:0x6960] == bytes.fromhex(
        "0004FFFC0004FFFC0003FFFD0003FFFD"
        "0002FFFE0002FFFE0001FFFF0001FFFF0000"
    )


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


def test_menu_palette_record_bank_has_exact_record_partition():
    symbols = SymbolStore()
    bank = symbols.at(0x001296B2, include_ranges=False)
    assert bank is not None
    assert bank.name == "MENU_PALETTE_RECORD_BANK_1296B2"
    assert bank.end == 0x001297F1
    assert bank.size == 0x140
    assert bank.metadata["type"] == "palette_data"

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    payload = rom[0x001296B2:0x001297F2]
    assert len(payload) == 10 * 0x20
    words = [int.from_bytes(payload[offset:offset + 2], "big") for offset in range(0, len(payload), 2)]
    assert len(words) == 10 * 16
    assert all(word <= 0x0EEE and word & 0x1111 == 0 for word in words)
    assert symbols.at(0x001297F2, include_ranges=False).name == "MENU_PALETTE_BAND1_SOURCE"


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


def test_scene_transition_closing_and_sound_test_streams_are_exact():
    symbols = SymbolStore()
    closing = symbols.at(0x00128E45, include_ranges=False)
    assert closing is not None
    assert closing.name == "SCENE_TRANSITION_CLOSING_STREAM"
    assert closing.end == 0x00128E4A
    assert closing.size == 6
    assert closing.metadata["type"] == "scene_resource_stream"

    embedded = symbols.at(0x00128E49, include_ranges=False)
    assert embedded is not None
    assert embedded.name == "SCENE_TRANSITION_CLOSING_STREAM_BASE0000_ENTRY"
    assert embedded.metadata["alias_of"] == "SCENE_TRANSITION_CLOSING_STREAM"
    assert embedded.metadata["entry_offset"] == 4

    sound_test = symbols.at(0x00128E4D, include_ranges=False)
    assert sound_test is not None
    assert sound_test.name == "SOUND_TEST_PRESENTATION_STREAM"
    assert sound_test.end == 0x00128E4E
    assert sound_test.size == 2
    assert sound_test.metadata["type"] == "scene_resource_stream"

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x00128E45:0x00128E4B] == bytes.fromhex("032820000800")
    assert rom[0x00128E4D:0x00128E4F] == bytes.fromhex("0A00")


def test_menu_sound_test_shared_presentation_stream_has_exact_terminal():
    symbols = SymbolStore()
    stream = symbols.at(0x00128E5B, include_ranges=False)
    assert stream is not None
    assert stream.name == "MENU_SOUND_TEST_SHARED_PRESENTATION_STREAM"
    assert stream.end == 0x00128EB0
    assert stream.size == 0x56
    assert stream.metadata["type"] == "scene_resource_stream"

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x00128E5B] == 0x0A
    assert rom[0x00128E5C:0x00128E84] == bytes([0x20]) * 40
    assert rom[0x00128E84:0x00128E88] == bytes.fromhex("01D80201")
    assert rom[0x00128E88:0x00128EB0] == bytes([0x20]) * 40
    assert rom[0x00128EB0] == 0x00
    assert symbols.at(0x00128ED2, include_ranges=False).name == "SCENE_BLANK_PALETTE"


def test_extended_actor_collision_handler_dispatch_family_is_exact():
    symbols = SymbolStore()
    expected_functions = {
        0x001ABF8E: (0x001ABF99, "ActorType02_08_09_ActorCollisionHandler"),
        0x001ABF9A: (0x001ABF9B, "ActorType01_ActorCollisionHandler"),
        0x001ABFF0: (0x001ABFF9, "ActorType23_ActorCollisionHandler"),
        0x001AC03E: (0x001AC059, "ActorType24_ActorCollisionHandler"),
        0x001AC05A: (0x001AC075, "ActorType25_ActorCollisionHandler"),
        0x001AC076: (0x001AC07B, "ActorType26_ActorCollisionHandler"),
        0x001AC07C: (0x001AC097, "ActorType27_ActorCollisionHandler"),
        0x001AC098: (0x001AC0B9, "ActorType28_ActorCollisionHandler"),
        0x001AC0EE: (0x001AC101, "ActorType03_ActorCollisionHandler"),
        0x001AC102: (0x001AC1B3, "ActorType04_ActorCollisionHandler"),
        0x001AC1B4: (0x001AC1CF, "ActorType05_ActorCollisionHandler"),
        0x001AC2E0: (0x001AC2FB, "ActorType1F_ActorCollisionHandler"),
        0x001AC2FC: (0x001AC317, "ActorType22_ActorCollisionHandler"),
        0x001AC408: (0x001AC431, "ActorType1A_ActorCollisionHandler"),
        0x001AC432: (0x001AC443, "ActorType1B_ActorCollisionHandler"),
        0x001AC444: (0x001AC455, "ActorType1C_ActorCollisionHandler"),
        0x001AC4DE: (0x001AC4E7, "ActorType18_19_ActorCollisionHandler"),
    }
    for address, (end, name) in expected_functions.items():
        function = symbols.at(address, include_ranges=False)
        assert function is not None
        assert function.name == name
        assert function.end == end
        assert function.size == end - address + 1

    expected_pointers = {
        0x001EBE: (0x01, 0x001ABF9A),
        0x001EC2: (0x02, 0x001ABF8E),
        0x001EC6: (0x03, 0x001AC0EE),
        0x001ECA: (0x04, 0x001AC102),
        0x001ECE: (0x05, 0x001AC1B4),
        0x001EDA: (0x08, 0x001ABF8E),
        0x001EDE: (0x09, 0x001ABF8E),
        0x001F1A: (0x18, 0x001AC4DE),
        0x001F1E: (0x19, 0x001AC4DE),
        0x001F22: (0x1A, 0x001AC408),
        0x001F26: (0x1B, 0x001AC432),
        0x001F2A: (0x1C, 0x001AC444),
        0x001F36: (0x1F, 0x001AC2E0),
        0x001F42: (0x22, 0x001AC2FC),
        0x001F46: (0x23, 0x001ABFF0),
        0x001F4A: (0x24, 0x001AC03E),
        0x001F4E: (0x25, 0x001AC05A),
        0x001F52: (0x26, 0x001AC076),
        0x001F56: (0x27, 0x001AC07C),
        0x001F5A: (0x28, 0x001AC098),
    }
    rom = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    data = rom.read_bytes()
    for address, (actor_type, target) in expected_pointers.items():
        pointer = symbols.at(address, include_ranges=False)
        assert pointer is not None
        assert pointer.metadata["type"] == "rom_pointer"
        assert int.from_bytes(data[address:address + 4], "big") == target
        assert address == 0x001EBA + actor_type * 4


def test_final_mechanical_function_closure_is_exact():
    symbols = SymbolStore()
    expected = {
        0x001AE6B4: (0x001AE6BB, "Player_SuppressCollisionResponse"),
        0x001AF562: (0x001AF56B, "ActorType5D_PlayerCollisionHandler"),
        0x001AF56C: (0x001AF58F, "PlayerCollision_ClearMatchingActorTypeRecords"),
        0x001B03BE: (0x001B03F1, "InteractionCounter_DecrementSecondaryDigits"),
    }
    for address, (end, name) in expected.items():
        function = symbols.at(address, include_ranges=False)
        assert function is not None
        assert function.name == name
        assert function.end == end
        assert function.size == end - address + 1

    suppression = symbols.at(0x00FFF0F5, include_ranges=False)
    assert suppression is not None
    assert suppression.name == "PLAYER_COLLISION_RESPONSE_SUPPRESS"
    assert suppression.metadata["type"] == "u8"

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001AE6B4:0x001AE6BC] == bytes.fromhex("50F900FFF0F54E75")
    assert rom[0x001B03BE:0x001B03F2][-2:] == bytes.fromhex("4E75")


def test_actor_type04_collision_animation_entry_is_exact():
    symbols = SymbolStore()
    stream = symbols.at(0x00124C18, include_ranges=False)
    assert stream is not None
    assert stream.name == "ACTOR_ANIM_TYPE04_COLLISION_RESPONSE"
    assert stream.end == 0x00124C39
    assert stream.size == 34
    assert stream.metadata["type"] == "animation_stream"

    nested = symbols.at(0x00124C1A, include_ranges=False)
    assert nested is not None
    assert nested.name == "ACTOR_ANIM_TYPE84_MOVING_CHILD_SPAWN_PREFIX"
    assert nested.metadata["alias_of"] == "ACTOR_ANIM_TYPE04_COLLISION_RESPONSE"
    assert nested.metadata["entry_offset"] == 2

    rom_path = Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin"
    rom = load_animation_decoder().RomReader(rom_path.read_bytes())
    decoded = load_animation_decoder().AnimationDecoder(rom).decode_stream(
        0x00124C18,
        max_instructions=64,
        max_bytes=64,
        follow_control_flow=False,
    )
    assert decoded["bytes_decoded"] == 34
    assert decoded["stopped_reason"] == "unconditional_jump"
    assert decoded["instructions"][-1]["branch_target"] == "0x00124BDC"


def test_actor_animation_callback_family_is_exact():
    symbols = SymbolStore()
    expected = {
        0x001ACC18: (0x001ACC1F, "ActorEvent_SetActorField3CBit5"),
        0x001ACC20: (0x001ACC27, "ActorEvent_ClearActorField3CBit5"),
        0x001ACC28: (0x001ACC2F, "ActorEvent_SetActorActiveBit"),
        0x001ACC56: (0x001ACC5D, "ActorEvent_ClearActorActiveBit"),
        0x001ACC5E: (0x001ACD01, "ActorEvent_QueueRandomVariantAudio"),
        0x001ACD02: (0x001ACD53, "ActorEvent_QueueParityAudio"),
        0x001ACD5A: (0x001ACD7D, "ActorEvent_ApplyWideRandomOffsets"),
        0x001ACD7E: (0x001ACDA1, "ActorEvent_ApplyNarrowRandomOffsets"),
    }
    for address, (end, name) in expected.items():
        function = symbols.at(address, include_ranges=False)
        assert function is not None
        assert function.name == name
        assert function.end == end
        assert function.size == end - address + 1

    callback = symbols.at(0x001ACC5E, include_ranges=False)
    assert callback.aliases == ("AnimationExtendedInteractionCallback",)
    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001ACC18:0x001ACC20] == bytes.fromhex("08E90005003C4E75")
    assert rom[0x001ACC20:0x001ACC28] == bytes.fromhex("08A90005003C4E75")
    assert rom[0x001ACC28:0x001ACC30] == bytes.fromhex("08E9000000064E75")
    assert rom[0x001ACC56:0x001ACC5E] == bytes.fromhex("08A9000000064E75")


def test_extended_actor_animation_callback_family_is_exact():
    symbols = SymbolStore()
    expected = {
        0x001ACB5A: (0x001ACB61, "ActorEvent_SetActorField3CBit4"),
        0x001ACB62: (0x001ACB69, "ActorEvent_ClearActorField3CBit4"),
        0x001ACB8A: (0x001ACB91, "ActorEvent_SetActorFlagsBit4"),
        0x001ACB92: (0x001ACB99, "ActorEvent_ClearActorFlagsBit4"),
        0x001ACBD8: (0x001ACBF1, "ActorEvent_CopyLinkedActorCoordinates"),
    }
    for address, (end, name) in expected.items():
        function = symbols.at(address, include_ranges=False)
        assert function is not None
        assert function.name == name
        assert function.end == end
        assert function.size == end - address + 1

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001ACB5A:0x001ACB62] == bytes.fromhex("08E90004003C4E75")
    assert rom[0x001ACB62:0x001ACB6A] == bytes.fromhex("08A90004003C4E75")
    assert rom[0x001ACB8A:0x001ACB92] == bytes.fromhex("08E9000400064E75")
    assert rom[0x001ACB92:0x001ACB9A] == bytes.fromhex("08A9000400064E75")
    assert rom[0x001ACBD8:0x001ACBF2] == bytes.fromhex(
        "2E29003E67122F082047336800020002336800040004205F4E75"
    )


def test_actor_animation_response_helpers_are_exact():
    symbols = SymbolStore()
    expected = {
        0x001ACB18: (0x001ACB59, "ActorEvent_ReinitializeInteractionResponse"),
        0x001ACB9A: (0x001ACBD7, "ActorEvent_SpawnCollisionResponseChild"),
    }
    for address, (end, name) in expected.items():
        function = symbols.at(address, include_ranges=False)
        assert function is not None
        assert function.name == name
        assert function.end == end
        assert function.size == end - address + 1

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001ACB18:0x001ACB5A] == bytes.fromhex(
        "6100185808290003000667142A494DF9001B792C610017DC"
        "08ED00030006600C2A494DF9001B792C610017C80839000000"
        "FF7E28670450ED00092EB900FF7D9E4E75"
    )
    assert rom[0x001ACB9A:0x001ACBD8] == bytes.fromhex(
        "1E3900FF7E28020700070C070001662C3F00610016CC6622"
        "4DF9001B7ABC610017501B7C004000063B69000200023E290004"
        "0647000A3B470004301F4E75"
    )


def test_interaction_resource_progress_reset_is_exact():
    symbols = SymbolStore()
    function = symbols.at(0x001B0024, include_ranges=False)
    assert function is not None
    assert function.name == "Interaction_ResetResourceProgressCounter"
    assert function.end == 0x001B0039
    assert function.size == 22

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001B0024:0x001B003A] == bytes.fromhex(
        "41F900FF7E38303C000210FC003051C8FFFA10BC0000"
    )


def test_interaction_resource_delay_counter_helpers_are_exact():
    symbols = SymbolStore()
    expected = {
        0x001B019C: (0x001B01A3, "Interaction_AddResourceDelay1"),
        0x001B01A4: (0x001B01AB, "Interaction_AddResourceDelay7"),
    }
    for address, (end, name) in expected.items():
        function = symbols.at(address, include_ranges=False)
        assert function is not None
        assert function.name == name
        assert function.end == end
        assert function.size == end - address + 1

    ram = symbols.at(0x00FFF159, include_ranges=False)
    assert ram is not None
    assert ram.name == "INTERACTION_RESOURCE_DELAY_COUNTER"
    assert ram.metadata["format"] == "countdown"

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001B019C:0x001B01A4] == bytes.fromhex("523900FFF1594E75")
    assert rom[0x001B01A4:0x001B01AC] == bytes.fromhex("5E3900FFF1594E75")


def test_interaction_target_dispatch_helper_is_exact():
    symbols = SymbolStore()
    function = symbols.at(0x001B0316, include_ranges=False)
    assert function is not None
    assert function.name == "Interaction_DispatchTargetState"
    assert function.end == 0x001B0333
    assert function.size == 30

    ram = symbols.at(0x00FFF0EC, include_ranges=False)
    assert ram is not None
    assert ram.name == "INTERACTION_TARGET_CURRENT"

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001B0316:0x001B0334] == bytes.fromhex(
        "4244183900FFF0ECD844D8442F3040004A3900FFF0EC"
        "660650F900FFF0E6"
    )


def test_interaction_response_target_helper_is_exact():
    symbols = SymbolStore()
    function = symbols.at(0x001B0434, include_ranges=False)
    assert function is not None
    assert function.name == "Interaction_AdvanceResponseTarget"
    assert function.end == 0x001B044D
    assert function.size == 26

    current = symbols.at(0x00FFEFFA, include_ranges=False)
    assert current is not None
    assert current.name == "INTERACTION_RESPONSE_CURRENT"
    pending = symbols.at(0x00FFEFFB, include_ranges=False)
    assert pending is not None
    assert pending.name == "INTERACTION_RESPONSE_PENDING"

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001B0434:0x001B044E] == bytes.fromhex(
        "3F00103900FFEFFAB03900FFEFFB6406523900FFEFFA301F4E75"
    )


def test_vdp_interaction_digit_writers_are_exact():
    symbols = SymbolStore()
    expected = {
        0x001B044E: (0x001B045F, "VDP_WriteAsciiDigit"),
        0x001B0460: (0x001B0479, "VDP_WriteThreeDigitValue"),
        0x001B047A: (0x001B048F, "VDP_WriteDecimalDigit"),
    }
    for address, (end, name) in expected.items():
        function = symbols.at(address, include_ranges=False)
        assert function is not None
        assert function.name == name
        assert function.end == end
        assert function.size == end - address + 1

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001B044E:0x001B0460] == bytes.fromhex(
        "343CE00114190402003033C200C000004E75"
    )
    assert rom[0x001B0460:0x001B047A] == bytes.fromhex(
        "283C0000006461000012283C0000000A61000008283C00000001"
    )
    assert rom[0x001B047A:0x001B0490] == bytes.fromhex(
        "343C0010B68465065202968460F661001D1E52004E75"
    )


def test_scene_resource_thirty_one_vblank_loop_is_exact():
    symbols = SymbolStore()
    function = symbols.at(0x001B1AA0, include_ranges=False)
    assert function is not None
    assert function.name == "Frame_WaitThirtyOneVBlanks"
    assert function.end == 0x001B1AB5
    assert function.size == 22

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001B1AA0:0x001B1AB6] == bytes.fromhex(
        "383C001E3F04610009F6523900FF7E28381F51CCFFF0"
    )


def test_scene_resource_vdp_accumulator_stream_is_exact():
    symbols = SymbolStore()
    function = symbols.at(0x001B1DDA, include_ranges=False)
    assert function is not None
    assert function.name == "VDP_WriteAccumulatedWordStream"
    assert function.end == 0x001B1E09
    assert function.size == 48

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001B1DDA:0x001B1E0A] == bytes.fromhex(
        "4240424123FC7000000300C00004383C00DF363C01FFD0712000"
        "5402C04333C000C0000033C000C0000051CCFFEA4E75"
    )


def test_player_facing_launch_motion_helper_is_exact():
    symbols = SymbolStore()
    function = symbols.at(0x001AE61A, include_ranges=False)
    assert function is not None
    assert function.name == "Player_InitializeFacingLaunchMotion"
    assert function.end == 0x001AE64B
    assert function.size == 50

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001AE61A:0x001AE64C] == bytes.fromhex(
        "423900FFF0EB4A3900FF7E49661233FCFC0000FF7E58"
        "33FCFC0000FF7E5A4E7533FC040000FF7E58"
        "33FCFC0000FF7E5A4E75"
    )


def test_random_parity_callback_is_exact():
    symbols = SymbolStore()
    function = symbols.at(0x001B52FA, include_ranges=False)
    assert function is not None
    assert function.name == "Random_AdvanceUntilParityChanges"
    assert function.end == 0x001B5317
    assert function.size == 30

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001B52FA:0x001B5318] == bytes.fromhex(
        "3F00103900FFF1116100DD2E02070001BE0067F4"
        "13C700FFF111301F4E75"
    )


def test_scene_resource_loader_and_blank_wrapper_gaps_are_exact():
    symbols = SymbolStore()
    functions = {
        0x001B47BE: (18, "SceneResource_LoadE000Resource12CE06"),
        0x001B4CE6: (38, "SceneResource_RunBlankStream1277C5"),
        0x001B4E44: (38, "SceneResource_RunBlankStream127B60"),
    }
    for address, (size, name) in functions.items():
        function = symbols.at(address, include_ranges=False)
        assert function is not None
        assert function.name == name
        assert function.size == size
        assert function.end == address + size - 1

    streams = {
        0x001277C5: (0x6F, "SCENE_RESOURCE_BLANK_STREAM_1277C5"),
        0x00127B60: (0x72, "SCENE_RESOURCE_BLANK_STREAM_127B60"),
    }
    for address, (size, name) in streams.items():
        stream = symbols.at(address, include_ranges=False)
        assert stream is not None
        assert stream.name == name
        assert stream.size == size
        assert stream.end == address + size - 1
        assert stream.metadata["type"] == "scene_resource_stream"

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001B47BE:0x001B47D0] == bytes.fromhex(
        "41F90012CE0643F90000E0006100EC4A4E75"
    )
    assert rom[0x001B4CE6:0x001B4D0C] == bytes.fromhex(
        "6100FD9250F900FFEFFC41F9001277C5303C0004"
        "323C00056100D4F6423900FFEFFC6000FE54"
    )
    assert rom[0x001B4E44:0x001B4E6A] == bytes.fromhex(
        "6100FC3450F900FFEFFC41F900127B60303C0003"
        "323C000B6100D398423900FFEFFC6000FCF6"
    )
    assert rom[0x001277C5 + 0x6E] == 0x00
    assert rom[0x00127B60 + 0x71] == 0x00


def test_camera_scroll_cursor_callback_family_is_exact():
    symbols = SymbolStore()
    callbacks = {
        0x001B52D6: (12, "Camera_SetScrollDataCursor693E"),
        0x001B52E2: (12, "Camera_SetScrollDataCursor6952"),
        0x001B52EE: (12, "Camera_SetScrollDataCursor695A"),
    }
    for address, (size, name) in callbacks.items():
        callback = symbols.at(address, include_ranges=False)
        assert callback is not None
        assert callback.name == name
        assert callback.size == size
        assert callback.end == address + size - 1

    cursor = symbols.at(0x00FF7E1A, include_ranges=False)
    assert cursor is not None
    assert cursor.name == "CAMERA_SCROLL_DATA_CURSOR"

    table = symbols.at(0x0000693E, include_ranges=False)
    assert table is not None
    assert table.name == "CAMERA_SCROLL_DELTA_TABLE"
    assert table.size == 0x20
    assert table.end == 0x0000695D
    assert table.metadata["type"] == "rom_table"

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001B52D6:0x001B52E2] == bytes.fromhex(
        "23FC0000693E00FF7E1A4E75"
    )
    assert rom[0x001B52E2:0x001B52EE] == bytes.fromhex(
        "23FC0000695200FF7E1A4E75"
    )
    assert rom[0x001B52EE:0x001B52FA] == bytes.fromhex(
        "23FC0000695A00FF7E1A4E75"
    )
    assert rom[0x0000693E:0x0000695E] == bytes.fromhex(
        "0004FFFC0004FFFC0003FFFD0003FFFD"
        "0002FFFE0002FFFE0001FFFF0001FFFF"
    )
    for address in (0x001211D0, 0x001211E6):
        assert rom[address:address + 4] == bytes.fromhex("001B52D6")
    for address in (0x00121196, 0x001213FC, 0x00121426, 0x00121456):
        assert rom[address:address + 4] == bytes.fromhex("001B52E2")
    for address in (0x001211AC, 0x00121204):
        assert rom[address:address + 4] == bytes.fromhex("001B52EE")


def test_interaction_anchor_forward_spawn_is_exact():
    symbols = SymbolStore()
    function = symbols.at(0x001B5786, include_ranges=False)
    assert function is not None
    assert function.name == "InteractionAnchor_ForwardSpawn"
    assert function.end == 0x001B57C3
    assert function.size == 62

    template = symbols.at(0x001B792C, include_ranges=False)
    assert template is not None
    assert template.name == "ACTOR_TEMPLATE_TYPE_2D_INTERACTION_RESPONSE"

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001B5786:0x001B57C4] == bytes.fromhex(
        "61008B3A66364DF9001B792C61008B76322900026100D8960247001F"
        "0641001092473B410002322900046100D8800247001F064100109247"
        "3B4100044E75"
    )


def test_low_confidence_scene_terrain_services_are_closed():
    symbols = SymbolStore()
    expected = {
        0x001A8E3E: ("SceneScript_AdvanceState", "trace_validated"),
        0x001B2ACE: ("Scene_EnterTransitionMode", "decompiled"),
        0x001B315C: ("SceneScript_CompleteToState1", "decompiled"),
        0x001ADB36: ("Terrain_ContourLookupHelper", "decompiled"),
    }
    for address, (name, confidence) in expected.items():
        function = symbols.at(address, include_ranges=False)
        assert function is not None
        assert function.name == name
        assert function.confidence == confidence

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001ADB36:0x001ADB5C] == bytes.fromhex(
        "48E7FFFE42471E3900FF7E28E74F3010308748E780806100CCCC4CDF"
        "010130804CDF7FFF4E75"
    )
    assert rom[0x001B315C:0x001B319C] == bytes.fromhex(
        "0C39000100FFF57C6618207900FFF57610184A00670E13C000FFF156"
        "23C800FFF5764E75201F6100B0946100F5FC6100F51013FC000100FF"
        "7E264EF9001A8B24"
    )


def test_actor_terrain_collision_loop_owns_complete_body():
    symbols = SymbolStore()
    function = symbols.at(0x001ADB5C, include_ranges=False)
    assert function is not None
    assert function.name == "Actor_TerrainCollisionLoop"
    assert function.end == 0x001ADE35
    assert function.size == 0x2DA



def test_actor_resource_clear_a0_variant_is_exact():
    symbols = SymbolStore()
    function = symbols.at(0x001AE3A0, include_ranges=False)
    assert function is not None
    assert function.name == "Actor_ClearOwnedResourcesFromA0"
    assert function.end == 0x001AE3CD
    assert function.size == 46

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001AE3A0:0x001AE3CE] == bytes.fromhex(
        "2F0E3F002C68002ABDFC00000000671842A8002A42A8002E4240"
        "1028002942280029421E51C8FFFC301F2C5F4E75"
    )


def test_actor_extended_forward_slot_allocator_is_exact():
    symbols = SymbolStore()
    function = symbols.at(0x001AE2C2, include_ranges=False)
    assert function is not None
    assert function.name == "Actor_FindFreeSlotExtendedForward"
    assert function.end == 0x001AE2D9
    assert function.size == 24

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001AE2C2:0x001AE2DA] == bytes.fromhex(
        "4BF900FF7E82303C001E4A156708DAFC004251C8FFF64E75"
    )


def test_actor_interaction_value_a5_variant_is_exact():
    symbols = SymbolStore()
    function = symbols.at(0x001AE700, include_ranges=False)
    assert function is not None
    assert function.name == "Actor_PublishInteractionValueA5"
    assert function.end == 0x001AE721
    assert function.size == 34

    rom = (Path(__file__).resolve().parents[1] / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001AE700:0x001AE722] == bytes.fromhex(
        "4A2D0034671A2F0B3F0147F900FFAE87302D0032122D003417810000321F265F4E75"
    )
