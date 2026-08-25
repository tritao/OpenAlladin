"""Decode the compact scene-transition table and script records."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from common import load_yaml, parse_int


OPCODE_MEANINGS = {
    0x00: {
        "name": "set_state",
        "condition": "always",
        "description": "Consume the following word as SCENE_STATE.",
    },
    0x02: {
        "name": "set_state_if_wait_clear",
        "condition": "FFF005 == 0",
        "description": "Consume the following word as SCENE_STATE when the script wait flag is clear.",
    },
    0xFF: {
        "name": "reset_state_zero",
        "condition": "always",
        "description": "Reset the scene state through Scene_ResetToState0 and return.",
    },
}


def _u16(data: bytes, address: int) -> int:
    if address < 0 or address + 2 > len(data):
        raise ValueError(f"16-bit read outside ROM at 0x{address:06X}")
    return int.from_bytes(data[address:address + 2], "big")


def _u32(data: bytes, address: int) -> int:
    if address < 0 or address + 4 > len(data):
        raise ValueError(f"32-bit read outside ROM at 0x{address:06X}")
    return int.from_bytes(data[address:address + 4], "big")


def _hex(value: int, width: int = 6) -> str:
    return f"0x{value:0{width}X}"


def _preview(data: bytes, address: int, size: int = 8) -> str:
    if address < 0 or address >= len(data):
        return ""
    return data[address:min(address + size, len(data))].hex().upper()


def decode_scene_table(data: bytes, table: dict[str, Any]) -> dict[str, Any]:
    address = parse_int(table["address"])
    entry_size = parse_int(table["entry_size"])
    count = parse_int(table["count"])
    pointer_offset = parse_int(table["pointer_offset"])
    state_offset = parse_int(table["state_offset"])
    metadata_offset = parse_int(table["metadata_offset"])
    table_end = address + entry_size * count
    entries = []
    for index in range(count):
        entry_address = address + index * entry_size
        pointer = _u32(data, entry_address + pointer_offset)
        state = data[entry_address + state_offset]
        metadata = data[entry_address + metadata_offset]
        in_rom = pointer < len(data)
        entries.append({
            "index": index,
            "address": _hex(entry_address),
            "pointer": _hex(pointer, 8),
            "state": _hex(state, 2),
            "metadata": _hex(metadata, 2),
            "pointer_in_rom": in_rom,
            "pointer_alignment": pointer & 1,
            "pointer_overlaps_table": address <= pointer < table_end,
            "pointer_preview": _preview(data, pointer) if in_rom else "",
        })
    return {
        "address": _hex(address),
        "entry_size": entry_size,
        "count": count,
        "entries": entries,
    }


def decode_scene_script(data: bytes, script: dict[str, Any]) -> dict[str, Any]:
    address = parse_int(script["address"])
    end = parse_int(script["end"])
    record_size = parse_int(script.get("record_size", 4))
    if record_size != 4:
        raise ValueError("scene script decoder currently requires 4-byte records")
    if end > len(data):
        raise ValueError(f"scene script end 0x{end:06X} is outside the ROM")
    records = []
    cursor = address
    while cursor + record_size <= end:
        opcode = _u16(data, cursor)
        argument = _u16(data, cursor + 2)
        meaning = OPCODE_MEANINGS.get(opcode, {
            "name": "gated_state_record",
            "condition": "FFF176 != 0",
            "description": "Run the gated script helper and consume the following word as SCENE_STATE.",
        })
        records.append({
            "address": _hex(cursor),
            "opcode": _hex(opcode, 4),
            "argument": _hex(argument, 4),
            "operation": meaning["name"],
            "condition": meaning["condition"],
            "description": meaning["description"],
        })
        cursor += record_size
    return {
        "name": script.get("name"),
        "address": _hex(address),
        "cursor": _hex(parse_int(script["cursor"])) if script.get("cursor") is not None else None,
        "end": _hex(end),
        "record_size": record_size,
        "records": records,
        "end_bytes": _preview(data, end, 8),
    }


def extract_scene_transitions(
    data: bytes,
    metadata_path: Path,
    output_path: Path,
) -> dict[str, Any]:
    metadata = load_yaml(metadata_path) or {}
    result = {
        "format": "openaladdin-scene-transitions-report-v1",
        "rom_size": len(data),
        "metadata": str(metadata_path),
        "table": decode_scene_table(data, metadata["table"]),
        "scripts": [decode_scene_script(data, script) for script in metadata.get("scripts", [])],
        "semantics": {
            "scene_state_address": "0x00FF7E26",
            "scene_script_cursor_address": "0x00FFF572",
            "scene_table_index_address": "0x00FFF57A",
            "script_countdown_address": "0x00FFF0E9",
            "script_wait_address": "0x00FFF005",
            "script_gate_address": "0x00FFF176",
            "state_write_instruction": "0x001A8ED2",
        },
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result
