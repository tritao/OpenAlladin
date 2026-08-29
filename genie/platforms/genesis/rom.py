"""Small utilities for recognizing Genesis ROM images."""

from __future__ import annotations


GENESIS_HEADER_OFFSET = 0x100
GENESIS_SYSTEM_ID = b"SEGA"


def has_genesis_header(data: bytes, offset: int = GENESIS_HEADER_OFFSET) -> bool:
    """Return whether *data* contains the Genesis system identifier."""

    return data[offset:offset + len(GENESIS_SYSTEM_ID)] == GENESIS_SYSTEM_ID


def is_genesis_rom(data: bytes, offset: int = GENESIS_HEADER_OFFSET) -> bool:
    """Compatibility-friendly alias for :func:`has_genesis_header`."""

    return has_genesis_header(data, offset)


__all__ = [
    "GENESIS_HEADER_OFFSET",
    "GENESIS_SYSTEM_ID",
    "has_genesis_header",
    "is_genesis_rom",
]
