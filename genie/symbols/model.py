"""Data model for canonical symbols.

The tracked YAML files remain the source of truth.  This model is deliberately
small and immutable so callers can use the same object from CLI commands,
analysis exporters, and future decompilation generators.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Mapping


@dataclass(frozen=True, slots=True)
class Symbol:
    """A normalized symbol from the reverse-engineering knowledge base."""

    address: int
    name: str
    kind: str
    confidence: str = "unknown"
    source: str = ""
    provenance: tuple[str, ...] = ()
    aliases: tuple[str, ...] = ()
    size: int | None = None
    end: int | None = None
    description: str | None = None
    metadata: Mapping[str, Any] = field(default_factory=dict)

    @property
    def category(self) -> str:
        """Compatibility spelling for code that still groups by YAML file."""

        return "functions" if self.kind == "function" else self.kind

    @property
    def range(self) -> tuple[int, int] | None:
        """Return the inclusive address range when one is known."""

        if self.end is not None:
            return self.address, self.end
        if self.size is not None and self.size > 0:
            return self.address, self.address + self.size - 1
        return None

    @property
    def is_mechanical(self) -> bool:
        from .naming import is_mechanical_name

        return is_mechanical_name(self.name)

    def contains(self, address: int) -> bool:
        """Whether *address* is the symbol address or lies in its known range."""

        address = int(address)
        symbol_range = self.range
        return address == self.address or bool(symbol_range and symbol_range[0] <= address <= symbol_range[1])

    def to_dict(self) -> dict[str, Any]:
        """Serialize the canonical fields in a stable JSON-friendly shape."""

        result: dict[str, Any] = {
            "address": self.address,
            "name": self.name,
            "kind": self.kind,
            "confidence": self.confidence,
            "source": self.source,
            "provenance": list(self.provenance),
            "aliases": list(self.aliases),
        }
        if self.size is not None:
            result["size"] = self.size
        if self.end is not None:
            result["end"] = self.end
        if self.range is not None:
            result["range"] = {"start": self.range[0], "end": self.range[1]}
        if self.description:
            result["description"] = self.description
        result.update(self.metadata)
        return result
