"""Range normalization and source-specific ROM extent helpers."""

from __future__ import annotations

from dataclasses import dataclass
import heapq
from typing import Iterable

from genie.common import parse_int

from .model import LayoutRange, coalesce_ranges


# Extents recovered from the dispatch-table ledgers.  Keeping these here
# avoids pretending that a one-byte canonical label describes a whole table.
KNOWN_EXTENTS = {
    "PLAYER_COLLISION_HANDLER_TABLE": (0x00001CBE, 0x00001DBD),
    "ACTOR_COLLISION_HANDLER_TABLE": (0x00001EBA, 0x00001FB9),
    "INTERACTION_HANDLER_TABLE": (0x00004154, 0x00004553),
    "TERRAIN_RESPONSE_HANDLER_TABLE": (0x00004554, 0x00004953),
    "ACTOR_VM_DISPATCH_TABLE": (0x00004954, 0x000049A7),
}


@dataclass(frozen=True, slots=True)
class Candidate:
    start: int
    end: int
    layout_class: str
    source: str
    priority: int
    name: str | None = None
    evidence: tuple[str, ...] = ()

    def clipped(self, rom_size: int) -> "Candidate | None":
        start = max(0, self.start)
        end = min(rom_size - 1, self.end)
        if start > end:
            return None
        return Candidate(start, end, self.layout_class, self.source, self.priority, self.name, self.evidence)


def partition(candidates: Iterable[Candidate], rom_size: int) -> tuple[LayoutRange, ...]:
    """Turn overlapping evidence into one deterministic, gap-filled partition."""

    if rom_size <= 0:
        return ()
    values = [item.clipped(rom_size) for item in candidates]
    values = [item for item in values if item is not None]
    boundaries = {0, rom_size}
    starts: dict[int, list[int]] = {}
    ends: dict[int, list[int]] = {}
    for index, item in enumerate(values):
        boundaries.add(item.start)
        boundaries.add(item.end + 1)
        starts.setdefault(item.start, []).append(index)
        ends.setdefault(item.end + 1, []).append(index)

    # Heap entries use a precomputed rank for the string tie-breakers so the
    # active-set sweep preserves the same winner ordering as max() without
    # rescanning every candidate at every boundary.
    ranked = sorted(
        range(len(values)),
        key=lambda index: (
            values[index].priority,
            values[index].start,
            values[index].source,
            values[index].name or "",
        ),
        reverse=True,
    )
    rank = {index: position for position, index in enumerate(ranked)}
    ordered = sorted(boundaries)
    result: list[LayoutRange] = []
    active: set[int] = set()
    heap: list[tuple[int, int, int, int]] = []
    for left, right_exclusive in zip(ordered, ordered[1:]):
        for index in ends.get(left, ()):
            active.discard(index)
        for index in starts.get(left, ()):
            active.add(index)
            item = values[index]
            heapq.heappush(heap, (-item.priority, -item.start, rank[index], index))
        while heap and heap[0][3] not in active:
            heapq.heappop(heap)
        if heap:
            winner = values[heap[0][3]]
            result.append(LayoutRange(
                left,
                right_exclusive - 1,
                winner.layout_class,
                winner.source,
                winner.name,
                winner.evidence,
            ))
        else:
            result.append(LayoutRange(left, right_exclusive - 1, "UNKNOWN", "layout.gap"))
    return coalesce_ranges(result)


def extent_for_symbol(symbol) -> tuple[int, int]:
    """Return the safest known extent for a tracked symbol."""

    if symbol.name in KNOWN_EXTENTS:
        return KNOWN_EXTENTS[symbol.name]
    if symbol.range is not None:
        return symbol.range
    symbol_type = str(symbol.metadata.get("type", "")).lower()
    if symbol_type == "rom_pointer":
        return symbol.address, symbol.address + 3
    if symbol.name.endswith("_TABLE") and symbol_type.endswith("table"):
        # A point label is still useful evidence. Do not infer an unbounded
        # table unless its extent is independently recorded above.
        return symbol.address, symbol.address
    return symbol.address, symbol.address


__all__ = ["Candidate", "KNOWN_EXTENTS", "extent_for_symbol", "partition"]
