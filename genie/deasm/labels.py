"""Stable labels for layout ranges."""

from __future__ import annotations

import re
from typing import Iterable

from genie.layout.model import LayoutRange
from genie.symbols import SymbolStore


_GENERATED_LABEL = re.compile(
    r"^(?:Func|Data|Table|Ram|Unknown|UnknownData|OpaqueData|"
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


__all__ = ["is_mechanical_label", "range_label", "range_labels"]
