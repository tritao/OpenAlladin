"""Validation for the normalized ROM layout partition."""

from __future__ import annotations

from .model import LAYOUT_CLASSES, Layout


def validate_layout(layout: Layout) -> list[str]:
    errors: list[str] = []
    if layout.rom_size <= 0:
        errors.append(f"ROM size must be positive, got {layout.rom_size}")
        return errors
    if not layout.ranges:
        errors.append("layout contains no ranges")
        return errors
    cursor = 0
    for item in layout.ranges:
        if item.layout_class not in LAYOUT_CLASSES:
            errors.append(f"unsupported layout class {item.layout_class!r} at 0x{item.start:08X}")
        if item.start < 0 or item.end < item.start or item.end >= layout.rom_size:
            errors.append(
                f"range outside ROM: 0x{item.start:08X}-0x{item.end:08X} (size {layout.rom_size})"
            )
        if item.start != cursor:
            relation = "overlap" if item.start < cursor else "gap"
            errors.append(f"layout {relation} before 0x{item.start:08X}; expected 0x{cursor:08X}")
        cursor = max(cursor, item.end + 1)
    if cursor != layout.rom_size:
        errors.append(f"layout ends at 0x{cursor:08X}, expected 0x{layout.rom_size:08X}")
    return errors


__all__ = ["validate_layout"]
