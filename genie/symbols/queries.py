"""Small query functions shared by CLI and future analysis consumers."""

from __future__ import annotations

from pathlib import Path

from .model import Symbol
from .store import SymbolStore


def symbol_at(address: int, *, store: SymbolStore | None = None) -> Symbol | None:
    return (store or SymbolStore()).at(address)


def find_symbols(query: str, *, kind: str | None = None, store: SymbolStore | None = None) -> tuple[Symbol, ...]:
    return (store or SymbolStore()).find(query, kind=kind)


def list_symbols(*, kind: str | None = None, store: SymbolStore | None = None) -> tuple[Symbol, ...]:
    return (store or SymbolStore()).list(kind=kind)


def validate_symbols(*, rom_size: int | None = None, root: Path | None = None) -> list[str]:
    store = SymbolStore(root=root) if root is not None else SymbolStore()
    return store.validate(rom_size=rom_size)
