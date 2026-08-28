"""Rendering helpers for the first, source-oriented 68000 emitter."""

from __future__ import annotations


def render_instruction(mnemonic: str, operands: str = "") -> str:
    text = str(mnemonic).strip()
    if not text:
        return ""
    operands = str(operands).strip()
    return f"    {text}{(' ' + operands) if operands else ''}"


def render_bytes(data: bytes, *, directive: str = "dc.b", width: int = 16) -> list[str]:
    """Render raw bytes in bounded deterministic directive lines."""

    if width <= 0:
        raise ValueError(f"directive width must be positive, got {width}")
    result = []
    for offset in range(0, len(data), width):
        chunk = data[offset:offset + width]
        values = ",".join(f"${value:02X}" for value in chunk)
        result.append(f"    {directive:<7} {values}")
    return result


def render_longs(data: bytes, *, width: int = 4) -> list[str]:
    """Render aligned big-endian longwords for pointer-like tables."""

    if width != 4:
        raise ValueError("68000 longword directives require four-byte entries")
    result = []
    complete = len(data) - (len(data) % width)
    for offset in range(0, complete, width):
        value = int.from_bytes(data[offset:offset + width], byteorder="big")
        result.append(f"    {'dc.l':<7} ${value:08X}")
    return result


__all__ = ["render_bytes", "render_instruction", "render_longs"]
