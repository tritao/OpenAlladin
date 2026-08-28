"""Loading, indexing, and validation for the canonical symbol database."""

from __future__ import annotations

from collections import Counter
from pathlib import Path
from typing import Any, Iterable

from genie.common import ROOT, load_yaml, parse_int

from .model import Symbol
from .naming import mechanical_name


_YAML_CATEGORIES = ("functions", "ram", "data")
_SYMBOL_KINDS = ("function", "ram", "data")


def _as_strings(value: Any) -> tuple[str, ...]:
    if value is None:
        return ()
    if isinstance(value, str):
        return (value,)
    if isinstance(value, (list, tuple, set)):
        return tuple(str(item) for item in value if str(item).strip())
    return (str(value),)


def _range_values(entry: dict[str, Any], address: int) -> tuple[int | None, int | None]:
    size = entry.get("size", entry.get("length"))
    end = entry.get("end", entry.get("range_end"))
    raw_range = entry.get("range")
    if isinstance(raw_range, dict):
        if end is None:
            end = raw_range.get("end", raw_range.get("stop"))
        if size is None:
            size = raw_range.get("size", raw_range.get("length"))
    elif isinstance(raw_range, (list, tuple)) and len(raw_range) >= 2:
        if end is None:
            end = raw_range[1]

    if size is None and entry.get("entry_size") is not None and entry.get("count") is not None:
        size = parse_int(entry["entry_size"]) * parse_int(entry["count"])
    parsed_size = parse_int(size) if size is not None else None
    parsed_end = parse_int(end) if end is not None else None
    if parsed_size is None and parsed_end is not None:
        parsed_size = parsed_end - address + 1
    if parsed_end is None and parsed_size is not None:
        parsed_end = address + parsed_size - 1
    return parsed_size, parsed_end


def _source_path(root: Path, category: str) -> str:
    return str((root / f"re/symbols/{category}.yml").relative_to(root))


def load_symbols(root: Path = ROOT) -> tuple[Symbol, ...]:
    """Load all tracked symbol maps into one deterministic sequence."""

    root = Path(root).resolve()
    symbols: list[Symbol] = []
    for category in _YAML_CATEGORIES:
        path = root / f"re/symbols/{category}.yml"
        document = load_yaml(path) or {}
        if not isinstance(document, dict):
            raise ValueError(f"{path.relative_to(root)} must contain an address map")
        source = _source_path(root, category)
        for raw_address, raw_entry in document.items():
            try:
                address = parse_int(raw_address)
            except (TypeError, ValueError):
                # Comments and non-address YAML keys are ignored by the legacy
                # normalizer; retaining that behavior keeps this API compatible.
                continue
            if raw_entry is not None and not isinstance(raw_entry, dict):
                raise ValueError(f"{source} symbol {raw_address} must contain a mapping")
            entry = dict(raw_entry or {})
            name = str(entry.get("name", "")).strip()
            if not name:
                raise ValueError(f"{source} symbol {raw_address} has no name")
            size, end = _range_values(entry, address)
            provenance = _as_strings(entry.get("provenance", entry.get("evidence")))
            aliases = _as_strings(entry.get("aliases"))
            known = {
                "name", "confidence", "source", "provenance", "evidence", "aliases",
                "size", "length", "end", "range", "range_end", "description",
            }
            metadata = {key: value for key, value in entry.items() if key not in known}
            symbols.append(
                Symbol(
                    address=address,
                    name=name,
                    kind="function" if category == "functions" else category,
                    confidence=str(entry.get("confidence", "unknown")),
                    source=str(entry.get("source", source)),
                    provenance=provenance,
                    aliases=aliases,
                    size=size,
                    end=end,
                    description=str(entry["description"]) if entry.get("description") is not None else None,
                    metadata=metadata,
                )
            )
    return tuple(sorted(symbols, key=lambda symbol: (symbol.address, symbol.kind, symbol.name)))


class SymbolStore:
    """Indexed view over canonical tracked symbols."""

    def __init__(self, symbols: Iterable[Symbol] | None = None, *, root: Path = ROOT):
        self.root = Path(root).resolve()
        self._symbols = tuple(symbols if symbols is not None else load_symbols(self.root))
        self._by_address = {symbol.address: symbol for symbol in self._symbols}
        self._by_name: dict[str, list[Symbol]] = {}
        for symbol in self._symbols:
            for name in (symbol.name, *symbol.aliases):
                self._by_name.setdefault(name.casefold(), []).append(symbol)

    @property
    def symbols(self) -> tuple[Symbol, ...]:
        return self._symbols

    def at(self, address: int, *, include_ranges: bool = True) -> Symbol | None:
        """Return the symbol at an address, preferring an exact symbol start."""

        address = parse_int(address)
        exact = self._by_address.get(address)
        if exact is not None or not include_ranges:
            return exact
        for symbol in self._symbols:
            if symbol.contains(address):
                return symbol
        return None

    def name_for(self, address: int, kind: str = "unknown") -> str:
        """Return a semantic name when known, otherwise a stable generated name."""

        symbol = self.at(address)
        return symbol.name if symbol is not None else mechanical_name(address, kind)

    def find(self, query: str, *, exact: bool = False, kind: str | None = None) -> tuple[Symbol, ...]:
        """Find semantic names and aliases case-insensitively."""

        query = str(query).casefold()
        candidates = self._symbols
        if exact:
            candidates = tuple(self._by_name.get(query, ()))
        else:
            candidates = tuple(
                symbol
                for symbol in candidates
                if query in symbol.name.casefold() or any(query in alias.casefold() for alias in symbol.aliases)
            )
        if kind:
            normalized = str(kind).casefold()
            candidates = tuple(symbol for symbol in candidates if symbol.kind.casefold() == normalized)
        return tuple(sorted(candidates, key=lambda symbol: (symbol.address, symbol.name)))

    def list(self, *, kind: str | None = None) -> tuple[Symbol, ...]:
        if kind is None:
            return self._symbols
        normalized = str(kind).casefold()
        return tuple(symbol for symbol in self._symbols if symbol.kind.casefold() == normalized)

    def validate(self, *, rom_size: int | None = None) -> list[str]:
        """Validate addresses, ranges, names, and index uniqueness."""

        errors: list[str] = []
        names: dict[str, Symbol] = {}
        addresses: dict[int, Symbol] = {}
        for symbol in self._symbols:
            if symbol.kind not in _SYMBOL_KINDS:
                errors.append(f"{symbol.source}: unsupported kind {symbol.kind!r} for {symbol.name}")
            if symbol.address < 0 or symbol.address > 0xFFFFFF:
                errors.append(f"{symbol.source}: address outside 24-bit space for {symbol.name}: 0x{symbol.address:X}")
            previous_address = addresses.get(symbol.address)
            if previous_address:
                errors.append(
                    f"duplicate symbol address: 0x{symbol.address:06X} ({previous_address.name}, {symbol.name})"
                )
            addresses[symbol.address] = symbol
            for name in (symbol.name, *symbol.aliases):
                key = name.casefold()
                previous_name = names.get(key)
                if previous_name and previous_name.address != symbol.address:
                    errors.append(f"duplicate symbol name: {name}")
                names[key] = symbol
            if symbol.kind == "ram" and not 0xFF0000 <= symbol.address <= 0xFFFFFF:
                errors.append(f"RAM symbol outside work RAM: {symbol.name} at 0x{symbol.address:06X}")
            if symbol.kind == "function" and rom_size is not None and not 0 <= symbol.address < rom_size:
                errors.append(f"function outside ROM: {symbol.name} at 0x{symbol.address:06X}")
            if symbol.kind != "ram" and symbol.end is not None and symbol.end > 0xFFFFFF:
                errors.append(f"symbol range outside 24-bit space: {symbol.name} through 0x{symbol.end:X}")
            if symbol.size is not None and symbol.size <= 0:
                errors.append(f"symbol range has non-positive size: {symbol.name}")
            if symbol.end is not None and symbol.end < symbol.address:
                errors.append(f"symbol range ends before start: {symbol.name}")
            if symbol.kind == "function" and rom_size is not None and symbol.end is not None and symbol.end >= rom_size:
                errors.append(f"function range outside ROM: {symbol.name} through 0x{symbol.end:06X}")
        return errors

    def stats(self) -> dict[str, Any]:
        by_kind = Counter(symbol.kind for symbol in self._symbols)
        by_confidence = Counter(symbol.confidence for symbol in self._symbols)
        return {
            "total": len(self._symbols),
            "by_kind": dict(sorted(by_kind.items())),
            "by_confidence": dict(sorted(by_confidence.items())),
            "aliases": sum(len(symbol.aliases) for symbol in self._symbols),
            "ranged": sum(symbol.range is not None for symbol in self._symbols),
        }
