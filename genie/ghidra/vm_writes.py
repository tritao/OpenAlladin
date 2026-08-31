"""Offline inventory of global-RAM writes encoded in AnimationVM streams."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from genie.common import parse_int
from genie.games.aladdin.vm.animation import AnimationDecoder, RomReader


def _stream_ranges(layout_path: Path) -> list[dict[str, Any]]:
    document = json.loads(layout_path.read_text(encoding="utf-8"))
    ranges = document.get("ranges", document) if isinstance(document, dict) else document
    if not isinstance(ranges, list):
        raise ValueError(f"layout does not contain ranges: {layout_path}")
    result = []
    for item in ranges:
        if not isinstance(item, dict) or str(item.get("class", "")).upper() != "ANIMATION_STREAM":
            continue
        try:
            start = parse_int(item["start"])
            end = parse_int(item["end"])
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(f"invalid animation stream range in {layout_path}: {item!r}") from error
        if start <= end:
            result.append({"start": start, "end": end, "name": item.get("name")})
    return sorted(result, key=lambda item: (item["start"], item["end"]))


def _command_shape(opcode: int, mode: int) -> tuple[int, int, str]:
    if opcode == 0xED:
        width_code = mode & 0x0F
        operation = "write"
    else:
        width_code = mode & 0x07
        operation = "subtract" if mode & 0x80 else "add"
    payload_size = 2 if width_code in (1, 2) else 4
    width = 1 if width_code == 1 else 2 if width_code == 2 else 4
    return payload_size, width, operation


def find_vm_writers(
    rom_path: Path,
    layout_path: Path,
    target: int,
) -> list[dict[str, Any]]:
    """Find exact ED/FA commands writing global RAM address *target*.

    The VM encodes global RAM as ``0xFF0000 + offset``.  ROM-direct modes are
    intentionally ignored, and decoding is restricted to layout ranges already
    classified as ``ANIMATION_STREAM`` to avoid reporting arbitrary byte
    patterns in graphics or opaque data.
    """

    rom = rom_path.read_bytes()
    decoder = AnimationDecoder(RomReader(rom))
    streams = _stream_ranges(layout_path)
    target = parse_int(target)
    results: list[dict[str, Any]] = []
    for stream in streams:
        start = stream["start"]
        end = min(stream["end"], len(rom) - 1)
        if start > end:
            continue
        decoded = decoder.decode_stream(
            start,
            max_instructions=max(1024, (end - start + 1) // 2 + 16),
            max_bytes=end - start + 1,
            follow_control_flow=False,
            continue_after_control_flow=True,
        )
        for instruction in decoded["instructions"]:
            if instruction.get("kind") != "command":
                continue
            opcode = int(str(instruction["opcode"]), 16)
            if opcode not in (0xED, 0xFA):
                continue
            address = parse_int(instruction["address"])
            raw = bytes.fromhex(str(instruction["raw"]))
            if len(raw) < 4:
                continue
            mode = raw[1]
            if (opcode == 0xED and mode & 0x10) or (opcode == 0xFA and mode & 0x40):
                continue
            offset = int.from_bytes(raw[2:4], "big")
            destination = 0xFF0000 + offset
            if destination != target:
                continue
            payload_size, width, operation = _command_shape(opcode, mode)
            if len(raw) < 4 + payload_size:
                continue
            payload = raw[4:4 + payload_size]
            value = int.from_bytes(payload, "big")
            if width == 1:
                value &= 0xFF
            results.append({
                "address": f"0x{address:08X}",
                "stream": stream.get("name") or f"AnimationStream_{start:08X}",
                "stream_range": f"0x{start:08X}-0x{stream['end']:08X}",
                "opcode": f"0x{opcode:02X}",
                "mode": f"0x{mode:02X}",
                "target": f"0x{destination:08X}",
                "operation": operation,
                "width": width,
                "value": f"0x{value:0{width * 2}X}",
                "bytes": raw[:4 + payload_size].hex().upper(),
            })
    return results
