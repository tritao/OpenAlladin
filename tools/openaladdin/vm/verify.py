#!/usr/bin/env python3
"""Verify that decoded VM IR records re-encode to the original ROM bytes.

The current decoders retain each source record's raw bytes. This validator
uses those bytes as the lossless encoder and checks them at their recorded ROM
addresses. It is deliberately strict about truncation and malformed records,
and provides the round-trip contract needed before a semantic encoder grows.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def address(value: str) -> int:
    return int(value, 0)


def raw_bytes(record: dict[str, Any]) -> bytes:
    raw = record.get("raw")
    if raw is None:
        raise ValueError("record has no raw bytes")
    if not isinstance(raw, str) or len(raw) % 2:
        raise ValueError("record raw bytes are not an even-length hex string")
    return bytes.fromhex(raw)


def check_record(rom: bytes, record: dict[str, Any], label: str) -> int:
    source = address(str(record["address"]))
    encoded = raw_bytes(record)
    actual = rom[source : source + len(encoded)]
    if actual != encoded:
        raise ValueError(
            f"{label} at 0x{source:08X}: encoded={encoded.hex().upper()} "
            f"ROM={actual.hex().upper()}"
        )
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("animation", "movement"))
    parser.add_argument("rom", type=Path)
    parser.add_argument("decoded", type=Path)
    args = parser.parse_args()
    rom = args.rom.resolve().read_bytes()
    document = json.loads(args.decoded.resolve().read_text(encoding="utf-8"))
    checked = 0
    streams = document.get("streams") or {}
    for name, stream in streams.items():
        if args.kind == "animation":
            for index, instruction in enumerate(stream.get("instructions", [])):
                if instruction.get("kind") == "error":
                    raise SystemExit(f"{name}[{index}]: decoder error: {instruction.get('message')}")
                checked += check_record(rom, instruction, f"{name}[{index}]")
        else:
            for step_index, step in enumerate(stream.get("steps", [])):
                step_address = address(str(step["address"]))
                source_delta = rom[step_address : step_address + 2]
                if len(source_delta) != 2:
                    raise SystemExit(f"{name}[{step_index}]: truncated step delta")
                expected_delta = bytes(((int(step.get("delta_x", 0)) & 0xFF), (int(step.get("delta_y", 0)) & 0xFF)))
                if source_delta != expected_delta:
                    raise SystemExit(
                        f"{name}[{step_index}] at 0x{step_address:08X}: "
                        f"delta={expected_delta.hex().upper()} ROM={source_delta.hex().upper()}"
                    )
                checked += 1
                for command_index, command in enumerate(step.get("commands", [])):
                    checked += check_record(rom, command, f"{name}[{step_index}].commands[{command_index}]")

    print(f"{args.kind} programs: {len(streams)}")
    print(f"Round-trip exact:   {checked}")
    print("Unknown records:    0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
