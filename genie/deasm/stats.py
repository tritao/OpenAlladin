"""Coverage and naming statistics for generated ROM deassembly."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from genie.symbols import SymbolStore

from .labels import is_mechanical_label, range_labels
from .model import DeasmInput


@dataclass(frozen=True, slots=True)
class DeasmStats:
    rom_size: int
    owned_bytes: int
    unowned_bytes: int
    ranges: int
    ranges_by_class: dict[str, int]
    bytes_by_class: dict[str, int]
    functions: int
    instructions: int
    semantic_names: int
    mechanical_names: int

    def to_dict(self) -> dict[str, Any]:
        return {
            "rom_size": self.rom_size,
            "owned_bytes": self.owned_bytes,
            "unowned_bytes": self.unowned_bytes,
            "ranges": self.ranges,
            "ranges_by_class": dict(sorted(self.ranges_by_class.items())),
            "bytes_by_class": dict(sorted(self.bytes_by_class.items())),
            "functions": self.functions,
            "instructions": self.instructions,
            "semantic_names": self.semantic_names,
            "mechanical_names": self.mechanical_names,
        }

    def render(self) -> str:
        lines = [
            f"ROM                         {self.rom_size:,} bytes",
            f"owned bytes                {self.owned_bytes:,}",
            f"unowned bytes              {self.unowned_bytes:,}",
            "",
        ]
        for class_name in sorted(self.bytes_by_class):
            lines.append(
                f"{class_name:<28} {self.bytes_by_class[class_name]:>10,} bytes"
            )
        lines.extend([
            "",
            f"ranges                      {self.ranges:,}",
            f"instructions                {self.instructions:,}",
            f"functions                   {self.functions:,}",
            f"semantic names              {self.semantic_names:,}",
            f"mechanical names            {self.mechanical_names:,}",
        ])
        return "\n".join(lines)


def collect(value: DeasmInput, symbols: SymbolStore) -> DeasmStats:
    """Calculate stats from the same validated inputs consumed by the emitter."""

    ranges_by_class: dict[str, int] = {}
    bytes_by_class: dict[str, int] = {}
    for item in value.layout.ranges:
        ranges_by_class[item.layout_class] = ranges_by_class.get(item.layout_class, 0) + 1
        bytes_by_class[item.layout_class] = bytes_by_class.get(item.layout_class, 0) + item.size

    labels = range_labels(value.layout.ranges, symbols)
    mechanical_names = sum(1 for label in labels.values() if is_mechanical_label(label))
    functions = {
        instruction.function
        for instruction in value.instructions
        if instruction.function is not None
    }
    return DeasmStats(
        rom_size=len(value.rom),
        owned_bytes=sum(item.size for item in value.layout.ranges),
        unowned_bytes=0,
        ranges=len(value.layout.ranges),
        ranges_by_class=ranges_by_class,
        bytes_by_class=bytes_by_class,
        functions=len(functions),
        instructions=len(value.instructions),
        semantic_names=len(labels) - mechanical_names,
        mechanical_names=mechanical_names,
    )


__all__ = ["DeasmStats", "collect"]
