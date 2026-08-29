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
