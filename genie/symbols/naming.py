"""Deterministic names for addresses without semantic names."""

from __future__ import annotations

import re


_MECHANICAL_RE = re.compile(r"^(?:Func|Data|Table|Ram|Unknown)_[0-9A-Fa-f]+$")
_LOW_INFORMATION_RE = re.compile(
    r"^(?:"
    r"ActorType[0-9A-Fa-f]+(?:_[0-9A-Fa-f]+)*_(?:Actor|Player)CollisionHandler"
    r"|ActorType[0-9A-Fa-f]+_PrepareRecoveryPlane"
    r")$"
)


def _address(value: int) -> int:
    value = int(value)
    if value < 0 or value > 0xFFFFFF:
        raise ValueError(f"address outside 24-bit space: {value:#x}")
    return value


def mechanical_name(address: int, kind: str = "unknown") -> str:
    """Return a stable generated name for *address*.

    Code/data names use the eight-digit form emitted by Ghidra while compact
    table names retain the six-digit Genesis address form used in project
    notes.  The distinction is cosmetic; the address remains authoritative.
    """

    address = _address(address)
    normalized = str(kind).strip().lower().replace("-", "_")
    prefix, width = {
        "function": ("Func", 8),
        "func": ("Func", 8),
        "data": ("Data", 8),
        "ram": ("Ram", 8),
        "table": ("Table", 6),
        "pointer_table": ("Table", 6),
        "unknown": ("Unknown", 8),
    }.get(normalized, ("Unknown", 8))
    return f"{prefix}_{address:0{width}X}"


def is_mechanical_name(name: str) -> bool:
    """Whether *name* has the generated address-name shape."""

    return bool(_MECHANICAL_RE.fullmatch(str(name).strip()))


def is_low_information_name(name: str) -> bool:
    """Whether a name is stable but still too type-oriented for semantic work."""

    return bool(_LOW_INFORMATION_RE.fullmatch(str(name).strip()))


def name_for(address: int, kind: str = "unknown", existing: str | None = None) -> str:
    """Keep a semantic name when present; otherwise generate one."""

    generated = mechanical_name(address, kind)
    if existing is not None and str(existing).strip():
        candidate = str(existing).strip()
        if not is_mechanical_name(candidate) or candidate == generated:
            return candidate
    return generated
