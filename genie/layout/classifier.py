"""Evidence fusion for the first normalized ROM layout."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Iterable

from genie.common import ROOT, parse_int
from genie.ghidra.database import AnalysisDatabase
from genie.symbols import Symbol, SymbolStore

from .model import Layout
from .ranges import Candidate, extent_for_symbol, partition


def _class_for_symbol(symbol: Symbol) -> str:
    name = symbol.name.upper()
    symbol_type = str(symbol.metadata.get("type", "")).lower()
    if symbol.kind == "function":
        return "CODE"
    if name.startswith("ACTOR_MOVE_") or "MOVEMENT_STREAM" in symbol_type:
        return "MOVEMENT_STREAM"
    if "ACTOR_TEMPLATE" in name:
        return "ACTOR_TEMPLATE"
    if "ANIM" in name or "ANIMATION_STREAM" in symbol_type:
        return "ANIMATION_STREAM"
    if "ACTOR_FRAME" in name:
        return "GRAPHICS"
    if name == "LEVEL_TABLE":
        return "SCENE_TABLE"
    if symbol_type == "rom_pointer_table":
        return "POINTER_TABLE"
    if symbol_type == "rom_table":
        return "LEVEL_DATA"
    if "AUDIO" in name or "SOUND" in name:
        return "AUDIO_DATA"
    return "OPAQUE_DATA"


def _artifact_rom_size(document: dict[str, Any]) -> int | None:
    value = document.get("rom_size")
    if value is None and isinstance(document.get("rom"), dict):
        value = document["rom"].get("size")
    try:
        return parse_int(value) if value is not None else None
    except (TypeError, ValueError):
        return None


def _read_json(path: Path, rom_size: int) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(document, dict):
        return None
    artifact_size = _artifact_rom_size(document)
    return document if artifact_size in (None, rom_size) else None


def _stream_candidates(path: Path, rom_size: int, layout_class: str) -> list[Candidate]:
    document = _read_json(path, rom_size)
    if document is None:
        return []
    streams = document.get("streams", {})
    if isinstance(streams, dict):
        values = [(str(name), value) for name, value in streams.items()]
    elif isinstance(streams, list):
        values = [(str(value.get("name", "")), value) for value in streams if isinstance(value, dict)]
    else:
        return []
    result = []
    for name, stream in values:
        if not isinstance(stream, dict):
            continue
        try:
            start = parse_int(stream["entry"])
        except (KeyError, TypeError, ValueError):
            continue
        size = stream.get("bytes_decoded")
        try:
            size = parse_int(size) if size is not None else 0
        except (TypeError, ValueError):
            size = 0
        if size <= 0:
            instructions = stream.get("instructions", [])
            ends = []
            for instruction in instructions:
                if not isinstance(instruction, dict):
                    continue
                try:
                    ends.append(parse_int(instruction["address"]) + parse_int(instruction.get("size", 2)) - 1)
                except (KeyError, TypeError, ValueError):
                    continue
            end = max(ends, default=start)
        else:
            end = start + size - 1
        result.append(Candidate(
            start,
            end,
            layout_class,
            path.name,
            90,
            name or None,
            (path.name,),
        ))
    return result


def _asset_candidates(root: Path, rom_size: int) -> list[Candidate]:
    document = _read_json(root / "build/assets/manifest.json", rom_size)
    if document is None:
        return []
    result = []
    inventory = document.get("inventory", {})
    for block in inventory.get("rnc_blocks", []) if isinstance(inventory, dict) else []:
        if not isinstance(block, dict):
            continue
        try:
            start = parse_int(block["offset"])
            end = parse_int(block["end"])
        except (KeyError, TypeError, ValueError):
            continue
        refs = " ".join(str(item).lower() for item in block.get("references", ()) or ())
        if any(token in refs for token in (".chars", ".parallax", ".palette")):
            layout_class = "GRAPHICS"
        elif any(token in refs for token in (".floor", ".map", ".block")):
            layout_class = "LEVEL_DATA"
        else:
            layout_class = "COMPRESSED_DATA"
        result.append(Candidate(
            start,
            end,
            layout_class,
            "assets.manifest",
            85,
            ", ".join(str(item) for item in block.get("references", ()) or ()) or None,
            ("assets.manifest",),
        ))
    return result


def _audio_candidates(root: Path, rom_size: int) -> list[Candidate]:
    """Use the recovered Z80 stream decoder as bounded audio evidence."""
    document = _read_json(root / "build/re/z80-sound-driver/driver.json", rom_size)
    if document is None:
        return []
    streams = document.get("sequence_streams", {})
    ranges = streams.get("ranges", []) if isinstance(streams, dict) else []
    result = []
    for index, stream in enumerate(ranges):
        if not isinstance(stream, dict):
            continue
        try:
            start = parse_int(stream["start"])
            end = parse_int(stream["end_inclusive"])
        except (KeyError, TypeError, ValueError):
            continue
        owners = stream.get("owners", [])
        owner = owners[0] if isinstance(owners, list) and owners else {}
        sound_id = owner.get("sound_id", "unknown") if isinstance(owner, dict) else "unknown"
        track = owner.get("track", index) if isinstance(owner, dict) else index
        result.append(Candidate(
            start,
            end,
            "AUDIO_DATA",
            "z80.sequence_stream",
            92,
            f"AUDIO_Z80_SEQUENCE_STREAM_{sound_id}_{track:02d}",
            ("z80_sequence_stream_decoder",),
        ))
    return result


def _jump_table_candidates(database: AnalysisDatabase, rom_size: int) -> list[Candidate]:
    """Convert decompiler-recovered load tables into explicit JUMP_TABLE ranges."""

    document = database.load("jump_tables.json")
    tables = document.get("tables", []) if isinstance(document, dict) else document
    result = []
    for table in tables or []:
        if not isinstance(table, dict):
            continue
        for load in table.get("load_tables", []) or ():
            if not isinstance(load, dict):
                continue
            try:
                start = parse_int(load["address"])
                entry_size = parse_int(load["entry_size"])
                count = parse_int(load["count"])
            except (KeyError, TypeError, ValueError):
                continue
            if entry_size <= 0 or count <= 0:
                continue
            result.append(Candidate(
                start,
                start + entry_size * count - 1,
                "JUMP_TABLE",
                "ghidra.jump_table",
                95,
                table.get("function_name"),
                ("jump_tables.json",),
            ))
    return result


def _symbol_candidates(
    symbols: Iterable[Symbol],
    functions: dict[int, dict[str, Any]],
) -> list[Candidate]:
    result = []
    for symbol in symbols:
        if symbol.kind == "ram":
            continue
        if symbol.metadata.get("alias_of"):
            # Alternate entries describe control-flow into an existing ROM
            # object; they must not split or compete for layout ownership.
            continue
        if symbol.kind == "function" and symbol.address in functions:
            function = functions[symbol.address]
            ranges = _record_ranges(function)
        else:
            ranges = [extent_for_symbol(symbol)]
        for start, end in ranges:
            result.append(Candidate(
                start,
                end,
                _class_for_symbol(symbol),
                "tracked.symbol",
                100,
                symbol.name,
                tuple(symbol.provenance),
            ))
    return result


def _record_ranges(record: dict[str, Any]) -> list[tuple[int, int]]:
    """Return sparse ranges when an exporter preserved them, with fallback."""

    raw_ranges = record.get("ranges")
    if isinstance(raw_ranges, list) and raw_ranges:
        result = []
        for value in raw_ranges:
            if not isinstance(value, dict):
                continue
            try:
                start = parse_int(value["start"])
                end = parse_int(value["end"])
            except (KeyError, TypeError, ValueError):
                continue
            if start <= end:
                result.append((start, end))
        if result:
            return result
    try:
        return [(
            parse_int(record.get("start", record["address"])),
            parse_int(record.get("end", record["address"])),
        )]
    except (KeyError, TypeError, ValueError):
        return []


def build_layout(
    database: AnalysisDatabase | Path | None = None,
    *,
    root: Path = ROOT,
    include_artifacts: bool = True,
) -> Layout:
    """Build a gap-free ROM partition from Ghidra and tracked evidence."""

    if database is None:
        database = AnalysisDatabase()
    elif not isinstance(database, AnalysisDatabase):
        database = AnalysisDatabase(Path(database))
    root = Path(root).resolve()
    metadata = database.metadata
    rom_size = parse_int(metadata.get("rom_size"))
    symbols = SymbolStore(root=root).symbols
    candidates: list[Candidate] = []

    for function in database.functions:
        for start, end in _record_ranges(function):
            candidates.append(Candidate(start, end, "CODE", "ghidra.function", 40, function.get("name")))

    classes = database.load("address_classes.json")
    for item in classes.get("classes", []) if isinstance(classes, dict) else []:
        if not isinstance(item, dict) or str(item.get("source", "")) != "ghidra.defined_data":
            continue
        try:
            start = parse_int(item["start"])
            end = parse_int(item["end"])
        except (KeyError, TypeError, ValueError):
            continue
        candidates.append(Candidate(
            start,
            end,
            "OPAQUE_DATA",
            "ghidra.defined_data",
            50,
            item.get("name"),
        ))

    candidates.extend(_jump_table_candidates(database, rom_size))

    function_by_address = {}
    for function in database.functions:
        try:
            function_by_address[parse_int(function["address"])] = function
        except (KeyError, TypeError, ValueError):
            continue
    candidates.extend(_symbol_candidates(symbols, function_by_address))
    if include_artifacts:
        for relative, layout_class in (
            ("build/assets/animations.json", "ANIMATION_STREAM"),
            ("build/re/animation_streams.json", "ANIMATION_STREAM"),
            ("build/re/movement_streams.json", "MOVEMENT_STREAM"),
        ):
            candidates.extend(_stream_candidates(root / relative, rom_size, layout_class))
        candidates.extend(_asset_candidates(root, rom_size))
        candidates.extend(_audio_candidates(root, rom_size))

    ranges = partition(candidates, rom_size)
    sources = tuple(sorted({item.source for item in candidates}))
    return Layout(rom_size=rom_size, ranges=ranges, sources=sources)


__all__ = ["build_layout"]
