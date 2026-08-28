"""Small, reviewable edits to the canonical YAML symbol maps."""

from __future__ import annotations

import json
import re
from pathlib import Path

from genie.common import ROOT, parse_int

from .naming import mechanical_name
from .store import SymbolStore


_CATEGORIES = {"function": "functions", "ram": "ram", "data": "data"}
_ADDRESS_LINE = re.compile(r"^(0[xX][0-9A-Fa-f]+):\s*$")
_FIELD_LINE = re.compile(r"^  ([A-Za-z_][A-Za-z0-9_]*):(?:\s.*)?(?:\n|$)")
_LABEL = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def _scalar(value: str) -> str:
    """Return a YAML scalar without needing a YAML serializer dependency."""

    value = str(value)
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.-]*", value) and value not in {
        "true", "false", "null", "~",
    }:
        return value
    return json.dumps(value, ensure_ascii=False)


def _address_block(lines: list[str], address: int) -> tuple[int, int] | None:
    start = None
    for index, raw_line in enumerate(lines):
        match = _ADDRESS_LINE.match(raw_line.rstrip("\r\n"))
        if not match:
            continue
        if parse_int(match.group(1)) == address:
            start = index
            break
    if start is None:
        return None
    end = len(lines)
    for index in range(start + 1, len(lines)):
        if _ADDRESS_LINE.match(lines[index].rstrip("\r\n")):
            end = index
            break
    return start, end


def _set_field(lines: list[str], block: tuple[int, int], field: str, value: str) -> None:
    start, end = block
    replacement = f"  {field}: {_scalar(value)}\n"
    for index in range(start + 1, end):
        match = _FIELD_LINE.match(lines[index])
        if match and match.group(1) == field:
            lines[index] = replacement
            return

    # Keep the common fields easy to inspect: name first, then confidence,
    # then the descriptive annotation.
    order = {"name": 0, "confidence": 1, "description": 2}
    insert_at = start + 1
    field_order = order.get(field, 99)
    for index in range(start + 1, end):
        match = _FIELD_LINE.match(lines[index])
        if match and order.get(match.group(1), 99) <= field_order:
            insert_at = index + 1
    lines.insert(insert_at, replacement)


def _append_symbol(text: str, address: int, name: str, fields: dict[str, str]) -> str:
    if text.strip() in {"{}", "null", "~"}:
        text = ""
    if text and not text.endswith("\n"):
        text += "\n"
    block = [f"0x{address:08X}:\n", f"  name: {_scalar(name)}\n"]
    for field in ("confidence", "description"):
        if field in fields:
            block.append(f"  {field}: {_scalar(fields[field])}\n")
    return text + "".join(block)


def edit_symbol(
    address: int,
    *,
    root: Path = ROOT,
    kind: str | None = None,
    name: str | None = None,
    description: str | None = None,
    confidence: str | None = None,
) -> Symbol:
    """Create or update one canonical symbol and return the normalized result.

    The editor intentionally changes only scalar fields in the existing YAML
    block.  This keeps semantic edits small in review and leaves the tracked
    maps as the source of truth.
    """

    root = Path(root).resolve()
    address = parse_int(address)
    if not 0 <= address <= 0xFFFFFF:
        raise ValueError(f"address outside 24-bit space: 0x{address:X}")
    store = SymbolStore(root=root)
    existing = store.at(address, include_ranges=False)
    if existing is not None:
        if kind is not None and kind != existing.kind:
            raise ValueError(
                f"0x{address:08X} is already a {existing.kind} symbol; cannot edit it as {kind}"
            )
        resolved_kind = existing.kind
    else:
        resolved_kind = kind or "function"
    if resolved_kind not in _CATEGORIES:
        raise ValueError(f"unsupported symbol kind: {resolved_kind}")

    if name is not None and not _LABEL.fullmatch(name):
        raise ValueError(f"invalid symbol name {name!r}; use an assembler-safe identifier")
    resolved_name = name or (existing.name if existing is not None else mechanical_name(address, resolved_kind))
    fields: dict[str, str] = {}
    if confidence is not None:
        if not str(confidence).strip():
            raise ValueError("confidence cannot be empty")
        fields["confidence"] = str(confidence)
    if description is not None:
        fields["description"] = str(description)

    category = _CATEGORIES[resolved_kind]
    path = root / f"re/symbols/{category}.yml"
    if not path.is_file():
        raise FileNotFoundError(f"symbol map not found: {path}")
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    block = _address_block(lines, address)
    if block is None:
        append_fields = {"confidence": fields.get("confidence", "provisional")}
        append_fields.update({key: value for key, value in fields.items() if key != "confidence"})
        path.write_text(
            _append_symbol(text, address, resolved_name, append_fields),
            encoding="utf-8",
        )
    else:
        _set_field(lines, block, "name", resolved_name)
        for field, value in fields.items():
            # The block end moves when a field is inserted. Recompute it for
            # each update so annotations on sparse legacy entries are safe.
            current = _address_block(lines, address)
            if current is not None:
                _set_field(lines, current, field, value)
        path.write_text("".join(lines), encoding="utf-8")

    updated = SymbolStore(root=root).at(address, include_ranges=False)
    if updated is None:
        raise ValueError(f"could not reload edited symbol at 0x{address:08X}")
    return updated


__all__ = ["edit_symbol"]
