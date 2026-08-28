"""Immutable model for the normalized ROM layout database."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable

from genie.common import parse_int


LAYOUT_FORMAT = "openaladdin-rom-layout-v1"
LAYOUT_CLASSES = (
    "CODE",
    "POINTER_TABLE",
    "JUMP_TABLE",
    "ANIMATION_STREAM",
    "MOVEMENT_STREAM",
    "LEVEL_DATA",
    "ACTOR_TEMPLATE",
    "SCENE_TABLE",
    "AUDIO_DATA",
    "GRAPHICS",
    "COMPRESSED_DATA",
    "PADDING",
    "OPAQUE_DATA",
    "UNKNOWN",
)


@dataclass(frozen=True, slots=True)
class LayoutRange:
    start: int
    end: int
    layout_class: str
    source: str
    name: str | None = None
    evidence: tuple[str, ...] = ()

    @property
    def size(self) -> int:
        return self.end - self.start + 1

    def contains(self, address: int) -> bool:
        return self.start <= parse_int(address) <= self.end

    def to_dict(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "start": f"0x{self.start:08X}",
            "end": f"0x{self.end:08X}",
            "class": self.layout_class,
            "source": self.source,
        }
        if self.name:
            result["name"] = self.name
        if self.evidence:
            result["evidence"] = list(self.evidence)
        return result

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "LayoutRange":
        return cls(
            start=parse_int(value["start"]),
            end=parse_int(value["end"]),
            layout_class=str(value.get("class", "UNKNOWN")),
            source=str(value.get("source", "")),
            name=str(value["name"]) if value.get("name") is not None else None,
            evidence=tuple(str(item) for item in value.get("evidence", ()) or ()),
        )


@dataclass(frozen=True, slots=True)
class Layout:
    rom_size: int
    ranges: tuple[LayoutRange, ...]
    sources: tuple[str, ...] = ()

    @property
    def format(self) -> str:
        return LAYOUT_FORMAT

    def at(self, address: int) -> LayoutRange | None:
        address = parse_int(address)
        for item in self.ranges:
            if item.contains(address):
                return item
            if item.start > address:
                break
        return None

    def gaps(self) -> tuple[LayoutRange, ...]:
        return tuple(item for item in self.ranges if item.layout_class == "UNKNOWN")

    def to_dict(self) -> dict[str, Any]:
        counts: dict[str, int] = {}
        bytes_by_class: dict[str, int] = {}
        for item in self.ranges:
            counts[item.layout_class] = counts.get(item.layout_class, 0) + 1
            bytes_by_class[item.layout_class] = bytes_by_class.get(item.layout_class, 0) + item.size
        return {
            "format": LAYOUT_FORMAT,
            "rom_size": self.rom_size,
            "sources": list(self.sources),
            "counts": dict(sorted(counts.items())),
            "bytes": dict(sorted(bytes_by_class.items())),
            "ranges": [item.to_dict() for item in self.ranges],
        }

    @classmethod
    def from_dict(cls, value: dict[str, Any]) -> "Layout":
        if value.get("format") != LAYOUT_FORMAT:
            raise ValueError(
                f"layout format is {value.get('format')!r}, expected {LAYOUT_FORMAT!r}"
            )
        return cls(
            rom_size=parse_int(value["rom_size"]),
            sources=tuple(str(item) for item in value.get("sources", ()) or ()),
            ranges=tuple(LayoutRange.from_dict(item) for item in value.get("ranges", ()) or ()),
        )


def coalesce_ranges(ranges: Iterable[LayoutRange]) -> tuple[LayoutRange, ...]:
    result: list[LayoutRange] = []
    for item in ranges:
        if result:
            previous = result[-1]
            if (
                previous.end + 1 == item.start
                and previous.layout_class == item.layout_class
                and previous.source == item.source
                and previous.name == item.name
                and previous.evidence == item.evidence
            ):
                result[-1] = LayoutRange(
                    previous.start,
                    item.end,
                    previous.layout_class,
                    previous.source,
                    previous.name,
                    previous.evidence,
                )
                continue
        result.append(item)
    return tuple(result)


__all__ = ["LAYOUT_CLASSES", "LAYOUT_FORMAT", "Layout", "LayoutRange", "coalesce_ranges"]
