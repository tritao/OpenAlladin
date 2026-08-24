"""ROM pointer-reference discovery for the RNC asset corpus."""

from __future__ import annotations

import json
from pathlib import Path
import struct
from typing import Any


def _opcode_context(data: bytes, pointer_offset: int) -> tuple[str, int | None]:
    if pointer_offset < 2:
        return "table_or_unknown", None
    opcode = struct.unpack_from(">H", data, pointer_offset - 2)[0]
    if opcode in (0x4EB9, 0x4EF9):
        return ("jsr_absolute_long" if opcode == 0x4EB9 else "jmp_absolute_long"), opcode
    if opcode == 0x41F9:
        return "lea_absolute_long", opcode
    if opcode & 0xF1FF == 0x203C:
        return "move_l_immediate_dreg", opcode
    if opcode & 0xF1FF == 0x207C:
        return "move_l_immediate_areg", opcode
    return "table_or_unknown", opcode


def _pointer_tables(references: list[dict[str, Any]]) -> list[dict[str, Any]]:
    tables: list[dict[str, Any]] = []
    current: list[dict[str, Any]] = []
    previous: int | None = None

    def flush() -> None:
        if len(current) < 2:
            return
        tables.append({
            "start": current[0]["reference_address"],
            "end": f"0x{int(current[-1]['reference_address'], 16) + 3:06X}",
            "pointer_count": len(current),
            "target_offsets": [row["target_offset"] for row in current],
            "unassigned_targets": sum(row["target_unassigned"] for row in current),
            "references": current[:],
        })

    for reference in references:
        address = int(reference["reference_address"], 16)
        if current and previous is not None and address != previous + 4:
            flush()
            current = []
        current.append(reference)
        previous = address
    flush()
    return tables


def scan_rnc_references(data: bytes, corpus: dict[str, Any]) -> dict[str, Any]:
    """Find aligned big-endian 32-bit ROM pointers to every RNC block."""

    target_blocks = {}
    for block in corpus.get("blocks", []):
        target = int(block["offset"], 16)
        target_blocks[target] = block

    references: list[dict[str, Any]] = []
    by_target: dict[int, list[dict[str, Any]]] = {target: [] for target in target_blocks}
    for pointer_offset in range(0, max(0, len(data) - 3), 2):
        target = struct.unpack_from(">I", data, pointer_offset)[0]
        block = target_blocks.get(target)
        if block is None:
            continue
        context, opcode = _opcode_context(data, pointer_offset)
        reference = {
            "instruction_address": f"0x{pointer_offset - 2:06X}",
            "reference_address": f"0x{pointer_offset:06X}",
            "target_offset": f"0x{target:06X}",
            "target_unassigned": not bool(block.get("references")),
            "context": context,
            "opcode": f"0x{opcode:04X}" if opcode is not None else None,
        }
        references.append(reference)
        by_target[target].append(reference)

    target_rows = []
    for target, block in sorted(target_blocks.items()):
        target_rows.append({
            "offset": f"0x{target:06X}",
            "references": block.get("references", []),
            "unassigned": not bool(block.get("references")),
            "pointer_count": len(by_target[target]),
            "pointer_addresses": [row["reference_address"] for row in by_target[target]],
        })

    tables = _pointer_tables(references)
    return {
        "format": "openaladdin-rnc-pointer-references-v1",
        "target_count": len(target_blocks),
        "referenced_target_count": sum(bool(row["pointer_count"]) for row in target_rows),
        "unassigned_target_count": sum(row["unassigned"] for row in target_rows),
        "reference_count": len(references),
        "unassigned_reference_count": sum(row["target_unassigned"] for row in references),
        "pointer_table_count": len(tables),
        "targets": target_rows,
        "references": references,
        "pointer_tables": tables,
    }


def write_rnc_references(path: Path, result: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
