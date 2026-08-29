"""Stable labels for layout ranges."""

from __future__ import annotations

import re
from typing import Iterable

from genie.common import parse_int
from genie.layout.model import LayoutRange
from genie.symbols import SymbolStore


_GENERATED_LABEL = re.compile(
    r"^(?:Func|Data|Table|Ram|Unknown|UnknownData|OpaqueData|"
    r"Loc|"
    r"AnimationStream|MovementStream|JumpTable|PointerTable|SceneTable|"
    r"LevelData|ActorTemplate|AudioData|Graphics|CompressedData|Padding)_"
    r"[0-9A-Fa-f]+(?:_[0-9]+)?$"
)
_GHIDRA_AUTO_NAME = re.compile(
    r"^(?:FUN|SUB|DAT|LAB|PTR|OFF|STR|UNK|EXT|RAM)_[0-9A-Fa-f_]+$"
)

_CLASS_PREFIX = {
    "ANIMATION_STREAM": "AnimationStream",
    "MOVEMENT_STREAM": "MovementStream",
    "LEVEL_DATA": "LevelData",
    "TERRAIN_DATA": "TerrainData",
    "ACTOR_TEMPLATE": "ActorTemplate",
    "SCENE_TABLE": "SceneTable",
    "AUDIO_DATA": "AudioData",
    "GRAPHICS": "Graphics",
    "COMPRESSED_DATA": "CompressedData",
    "PADDING": "Padding",
    "OPAQUE_DATA": "OpaqueData",
    "UNKNOWN": "UnknownData",
    "JUMP_TABLE": "JumpTable",
    "POINTER_TABLE": "PointerTable",
}


def _safe_label(value: str) -> str:
    label = re.sub(r"[^A-Za-z0-9_]", "_", str(value).strip())
    if not label:
        return "UnknownData"
    if label[0].isdigit():
        return "_" + label
    return label


def is_mechanical_label(value: str) -> bool:
    return bool(_GENERATED_LABEL.fullmatch(str(value).strip()))


def _candidate(item: LayoutRange, symbols: SymbolStore) -> str | None:
    symbol = symbols.at(item.start, include_ranges=False)
    if symbol is not None:
        return symbol.name
    # Ghidra's broad function bodies can win many partition fragments.  Their
    # display name belongs to the entry point, not to every fragment, so only
    # keep non-canonical names from evidence that describes the fragment
    # itself.  Canonical tracked symbols above remain authoritative.
    if item.source in {"ghidra.function", "ghidra.jump_table"}:
        return None
    if item.name and not _GHIDRA_AUTO_NAME.fullmatch(str(item.name)) and not str(item.name).startswith("s_"):
        return str(item.name)
    return None


def range_label(item: LayoutRange, symbols: SymbolStore) -> str:
    candidate = _candidate(item, symbols)
    if candidate:
        return _safe_label(candidate)
    if item.layout_class == "CODE":
        return symbols.name_for(item.start, "function")
    prefix = _CLASS_PREFIX.get(item.layout_class, "Data")
    return f"{prefix}_{item.start:08X}"


def range_labels(ranges: Iterable[LayoutRange], symbols: SymbolStore) -> dict[int, str]:
    """Return unique labels keyed by range start in deterministic order."""

    result: dict[int, str] = {}
    used: set[str] = set()
    for item in ranges:
        base = range_label(item, symbols)
        label = base
        suffix = 2
        while label in used:
            label = f"{base}_{suffix}"
            suffix += 1
        used.add(label)
        result[item.start] = label
    return result


def _unique_label(candidate: str, used: set[str]) -> str:
    base = _safe_label(candidate)
    label = base
    suffix = 2
    while label in used:
        label = f"{base}_{suffix}"
        suffix += 1
    used.add(label)
    return label


def build_label_universe(
    ranges: Iterable[LayoutRange],
    instructions: Iterable[object],
    symbols: SymbolStore,
    rom_size: int,
) -> dict[int, str]:
    """Build labels for ranges, entries, and all ROM control/data targets.

    Ghidra's reference records are the canonical source for target addresses;
    this deliberately does not attempt to reinterpret arbitrary numeric
    operands.  Only addresses inside the ROM can become source labels, so RAM
    and I/O references remain numeric until a future equate service exists.
    """

    instruction_items = tuple(instructions)
    instruction_starts = {int(item.address) for item in instruction_items}
    function_entries = {
        int(item.function)
        for item in instruction_items
        if getattr(item, "function", None) is not None
        and int(item.address) == int(item.function)
    }
    range_items = tuple(ranges)
    range_by_start = range_labels(range_items, symbols)
    result = dict(range_by_start)
    used = set(result.values())

    def add(address: int, candidate: str) -> None:
        if not 0 <= address < rom_size:
            return
        if address in result:
            return
        result[address] = _unique_label(candidate, used)

    # Canonical symbol starts are meaningful even when the layout classifier
    # split a larger evidence range around them.
    for symbol in symbols.symbols:
        if 0 <= symbol.address < rom_size:
            add(symbol.address, symbol.name)

    # Every recovered function entry is a useful stable target, including
    # functions that have not yet received a semantic tracked symbol.
    for address in sorted(function_entries):
        symbol = symbols.at(address, include_ranges=False)
        add(address, symbol.name if symbol is not None else f"Func_{address:08X}")

    targets: set[int] = set()
    for instruction in instruction_items:
        for reference in getattr(instruction, "references", ()):
            try:
                target = parse_int(reference["to"])
            except (KeyError, TypeError, ValueError):
                continue
            if 0 <= target < rom_size:
                targets.add(target)

    range_index = 0
    for address in sorted(targets):
        if address in result:
            continue
        symbol = symbols.at(address, include_ranges=False)
        if symbol is not None:
            add(address, symbol.name)
        elif address in function_entries:
            add(address, f"Func_{address:08X}")
        elif address in instruction_starts:
            add(address, f"Loc_{address:08X}")
        else:
            while range_index < len(range_items) and range_items[range_index].end < address:
                range_index += 1
            item = (
                range_items[range_index]
                if range_index < len(range_items) and range_items[range_index].contains(address)
                else None
            )
            if item is not None and item.layout_class == "CODE":
                add(address, f"Loc_{address:08X}")
            else:
                add(address, f"Data_{address:08X}")
    return result


__all__ = ["build_label_universe", "is_mechanical_label", "range_label", "range_labels"]
