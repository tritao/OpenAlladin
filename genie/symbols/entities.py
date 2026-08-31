"""Curated semantic mappings for numeric actor and event identities.

Numeric selectors are part of the original ROM ABI.  This model therefore
keeps technical type values alongside any semantic name instead of replacing
them.  The mapping file is intentionally curated; the type worklist may
suggest candidates, but it never invents a mapping from a number alone.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import re
from pathlib import Path
from typing import Any, Iterable, Mapping

from genie.common import ROOT, load_yaml, parse_int


MAPPING_SCOPES = frozenset({"actor", "event", "resource", "role", "technical"})
_LABEL = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def _strings(value: Any) -> tuple[str, ...]:
    if value is None:
        return ()
    if isinstance(value, str):
        return (value,)
    if isinstance(value, (list, tuple, set)):
        return tuple(str(item) for item in value if str(item).strip())
    return (str(value),)


def _integers(value: Any, *, field_name: str) -> tuple[int, ...]:
    values = value if isinstance(value, (list, tuple, set)) else (() if value is None else (value,))
    result: list[int] = []
    for item in values:
        try:
            parsed = parse_int(item)
        except (TypeError, ValueError) as error:
            raise ValueError(f"{field_name} contains an invalid integer: {item!r}") from error
        if parsed < 0 or parsed > 0xFFFFFF:
            raise ValueError(f"{field_name} value outside 24-bit space: {parsed:#x}")
        result.append(parsed)
    return tuple(result)


@dataclass(frozen=True, slots=True)
class SemanticMapping:
    """A reviewed mapping from technical ROM identity to semantic meaning."""

    name: str
    scope: str
    symbol_addresses: tuple[int, ...] = ()
    technical_types: tuple[int, ...] = ()
    confidence: str = "unknown"
    evidence: tuple[str, ...] = ()
    description: str | None = None
    metadata: Mapping[str, Any] = field(default_factory=dict)

    @classmethod
    def from_dict(cls, raw: Mapping[str, Any]) -> "SemanticMapping":
        name = str(raw.get("name", "")).strip()
        scope = str(raw.get("scope", "")).strip().casefold()
        if not name or not _LABEL.fullmatch(name):
            raise ValueError(f"invalid semantic mapping name: {name!r}")
        if scope not in MAPPING_SCOPES:
            raise ValueError(f"unsupported semantic mapping scope for {name}: {scope!r}")
        known = {
            "name", "scope", "symbol", "symbols", "symbol_address", "symbol_addresses",
            "technical_type", "technical_types", "type_id", "type_ids", "confidence",
            "evidence", "provenance", "description",
        }
        return cls(
            name=name,
            scope=scope,
            symbol_addresses=_integers(
                raw.get("symbol_addresses", raw.get("symbols", raw.get("symbol_address", raw.get("symbol")))),
                field_name=f"symbol_addresses for {name}",
            ),
            technical_types=_integers(
                raw.get("technical_types", raw.get("type_ids", raw.get("technical_type", raw.get("type_id")))),
                field_name=f"technical_types for {name}",
            ),
            confidence=str(raw.get("confidence", "unknown")),
            evidence=_strings(raw.get("evidence", raw.get("provenance"))),
            description=str(raw["description"]) if raw.get("description") is not None else None,
            metadata={key: value for key, value in raw.items() if key not in known},
        )

    def to_dict(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "name": self.name,
            "scope": self.scope,
            "symbol_addresses": [f"0x{value:08X}" for value in self.symbol_addresses],
            "technical_types": [f"0x{value:02X}" if value <= 0xFF else f"0x{value:04X}" for value in self.technical_types],
            "confidence": self.confidence,
            "evidence": list(self.evidence),
        }
        if self.description:
            result["description"] = self.description
        result.update(self.metadata)
        return result


def load_entity_mappings(root: Path = ROOT) -> tuple[SemanticMapping, ...]:
    """Load the curated semantic mapping registry, if it exists."""

    path = Path(root).resolve() / "re/entities/mappings.yml"
    if not path.is_file():
        return ()
    document = load_yaml(path) or {}
    raw_mappings = document.get("mappings", []) if isinstance(document, dict) else document
    if not isinstance(raw_mappings, list):
        raise ValueError(f"{path.relative_to(Path(root).resolve())} must contain a mappings list")
    mappings: list[SemanticMapping] = []
    for index, item in enumerate(raw_mappings):
        if not isinstance(item, dict):
            raise ValueError(f"semantic mapping {index} must contain a mapping")
        mappings.append(SemanticMapping.from_dict(item))
    return tuple(mappings)


def validate_entity_mappings(mappings: Iterable[SemanticMapping]) -> list[str]:
    """Validate uniqueness and address/type bounds in the curated registry."""

    errors: list[str] = []
    names: dict[str, SemanticMapping] = {}
    symbols: dict[int, SemanticMapping] = {}
    for mapping in mappings:
        previous = names.get(mapping.name.casefold())
        if previous is not None:
            errors.append(f"duplicate semantic mapping name: {mapping.name}")
        names[mapping.name.casefold()] = mapping
        if mapping.scope not in MAPPING_SCOPES:
            errors.append(f"unsupported semantic mapping scope: {mapping.name}: {mapping.scope}")
        if not mapping.confidence.strip():
            errors.append(f"empty semantic mapping confidence: {mapping.name}")
        if not mapping.symbol_addresses and not mapping.technical_types:
            errors.append(f"semantic mapping has no technical identity: {mapping.name}")
        if not mapping.evidence:
            errors.append(f"semantic mapping has no evidence: {mapping.name}")
        for address in mapping.symbol_addresses:
            previous = symbols.get(address)
            if previous is not None and previous.name != mapping.name:
                errors.append(
                    f"symbol address 0x{address:08X} mapped to both {previous.name} and {mapping.name}"
                )
            symbols[address] = mapping
        for value in mapping.technical_types:
            if not 0 <= value <= 0xFFFFFF:
                errors.append(f"technical type outside 24-bit space: {mapping.name}: 0x{value:X}")
    return errors


def mappings_by_symbol(mappings: Iterable[SemanticMapping]) -> dict[int, SemanticMapping]:
    result: dict[int, SemanticMapping] = {}
    for mapping in mappings:
        for address in mapping.symbol_addresses:
            result[address] = mapping
    return result


__all__ = [
    "MAPPING_SCOPES",
    "SemanticMapping",
    "load_entity_mappings",
    "mappings_by_symbol",
    "validate_entity_mappings",
]
