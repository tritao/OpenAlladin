#!/usr/bin/env python3
"""Extract and map the ROM-resident Genesis Z80 sound driver."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
from typing import Any

from genie.common import ROOT, load_yaml, parse_int, write_json


# Audio_Init (68K 0x001E573A) copies this inclusive ROM range to Z80 RAM at
# 0x0000 before releasing the sound CPU. Keep the source range explicit: it is
# the stable bridge from the 68K ROM dump to the native sound-driver analysis.
DRIVER_ROM_START = 0x001B8480
DRIVER_ROM_END = 0x001B9D06
DRIVER_Z80_START = 0x0000

QUEUE_CURSOR = 0x0036
QUEUE_READ_CURSOR = 0x0037
QUEUE_DATA = 0x1B40
QUEUE_SIZE = 0x40
QUEUE_CONSUMER = 0x01D1
COMMAND_DISPATCH = 0x0945

# Command 0x10 reads a 16-bit entry from this ROM-resident table, then copies
# the selected 33-byte header into Z80 RAM at 0x0BBD.  The table is bounded by
# the first header: its first entry is 0x00E4, so there are 0x72 two-byte IDs.
SEQUENCE_TABLE_BASE = 0x001BAF6F
SEQUENCE_HEADER_SIZE = 0x21
SEQUENCE_TRACK_COUNT = 16

COMMAND_HANDLERS = {
    0x00: 0x09D6,
    0x01: 0x09EA,
    0x02: 0x09F8,
    0x03: 0x09FE,
    0x04: 0x0A07,
    0x05: 0x0A4B,
    0x06: 0x0A54,
    0x07: 0x0A6D,
    0x0B: 0x0A96,
    0x0C: 0x0AB1,
    0x0D: 0x0AC9,
    0x0E: 0x0ACF,
    0x10: 0x0A84,
    0x12: 0x0A8D,
    0x14: 0x0AE6,
    0x16: 0x0AF2,
    0x17: 0x0B12,
    0x1A: 0x0B4F,
    0x1B: 0x0B64,
    0x1C: 0x0B75,
    0x1D: 0x0B7F,
    0x1E: 0x0A10,
    0x1F: 0x0BA8,
    0x20: 0x0BB4,
}


def hex_address(value: int) -> str:
    return f"0x{value:04X}"


def find_all(data: bytes, pattern: bytes) -> list[int]:
    return [
        offset
        for offset in range(len(data) - len(pattern) + 1)
        if data[offset:offset + len(pattern)] == pattern
    ]


def read_u16_le(data: bytes, address: int) -> int:
    if address < 0 or address + 2 > len(data):
        raise SystemExit(f"ROM read outside image at {address:#x}")
    return int.from_bytes(data[address:address + 2], "little")


def sequence_table_report(image: bytes) -> dict[str, Any]:
    """Decode the ROM pointer table consumed by Z80 command 0x10."""
    first_header_offset = read_u16_le(image, SEQUENCE_TABLE_BASE)
    if first_header_offset == 0 or first_header_offset % 2:
        raise SystemExit("sequence table has no even first-header offset")
    entry_count = first_header_offset // 2
    entries: dict[str, Any] = {}
    for sound_id in range(entry_count):
        table_address = SEQUENCE_TABLE_BASE + sound_id * 2
        header_offset = read_u16_le(image, table_address)
        header_address = SEQUENCE_TABLE_BASE + header_offset
        if header_address + SEQUENCE_HEADER_SIZE > len(image):
            raise SystemExit(
                f"sequence header for ID {sound_id:#x} exceeds ROM at {header_address:#x}"
            )
        track_count = image[header_address]
        if track_count > SEQUENCE_TRACK_COUNT:
            raise SystemExit(
                f"sequence header for ID {sound_id:#x} has {track_count} tracks"
            )
        track_offsets = [
            read_u16_le(image, header_address + 1 + index * 2)
            for index in range(SEQUENCE_TRACK_COUNT)
        ]
        entries[f"0x{sound_id:02X}"] = {
            "table_address": hex_address(table_address),
            "header_offset": hex_address(header_offset),
            "header_address": hex_address(header_address),
            "track_count": track_count,
            "track_offsets": [hex_address(offset) for offset in track_offsets[:track_count]],
            "track_addresses": [
                hex_address(SEQUENCE_TABLE_BASE + offset)
                for offset in track_offsets[:track_count]
            ],
        }
    return {
        "table_address": hex_address(SEQUENCE_TABLE_BASE),
        "entry_width": 2,
        "entry_count": entry_count,
        "entry_endianness": "little",
        "header_size": SEQUENCE_HEADER_SIZE,
        "track_count_offset": 0,
        "track_offsets_offset": 1,
        "track_offset_count": SEQUENCE_TRACK_COUNT,
        "track_offset_endianness": "little",
        "entries": entries,
    }


def build_report(rom: Path) -> tuple[dict[str, Any], bytes]:
    knowledge = load_yaml(ROOT / "re/sound/z80-driver.yml") or {}
    copy_map = knowledge.get("rom_copy", {})
    if (
        parse_int(copy_map.get("source_start")) != DRIVER_ROM_START
        or parse_int(copy_map.get("source_end_inclusive")) != DRIVER_ROM_END
        or parse_int(copy_map.get("z80_destination")) != DRIVER_Z80_START
        or parse_int(copy_map.get("size")) != DRIVER_ROM_END - DRIVER_ROM_START + 1
    ):
        raise SystemExit("re/sound/z80-driver.yml does not match the extractor range")
    knowledge_handlers = {
        parse_int(opcode): parse_int(address)
        for opcode, address in (knowledge.get("command_handlers", {}) or {}).items()
    }
    if knowledge_handlers != COMMAND_HANDLERS:
        raise SystemExit("re/sound/z80-driver.yml command handler map is stale")
    knowledge_sequence = knowledge.get("sequence_table", {}) or {}
    if (
        parse_int(knowledge_sequence.get("address")) != SEQUENCE_TABLE_BASE
        or parse_int(knowledge_sequence.get("entry_count")) != 0x72
        or parse_int(knowledge_sequence.get("header_size")) != SEQUENCE_HEADER_SIZE
    ):
        raise SystemExit("re/sound/z80-driver.yml sequence table map is stale")

    image = rom.read_bytes()
    end = DRIVER_ROM_END + 1
    if end > len(image):
        raise SystemExit(
            f"driver range {DRIVER_ROM_START:#x}..{DRIVER_ROM_END:#x} exceeds {rom}"
        )
    driver = image[DRIVER_ROM_START:end]
    report = {
        "format": "openaladdin-z80-sound-driver-v1",
        "rom": str(rom),
        "knowledge": "re/sound/z80-driver.yml",
        "rom_range": {
            "start": hex_address(DRIVER_ROM_START),
            "end_inclusive": hex_address(DRIVER_ROM_END),
            "size": len(driver),
        },
        "z80_load_range": {
            "start": hex_address(DRIVER_Z80_START),
            "end_inclusive": hex_address(DRIVER_Z80_START + len(driver) - 1),
        },
        "driver_sha256": hashlib.sha256(driver).hexdigest(),
        "entry_points": {
            "reset_stub": hex_address(0x0000),
            "initialization": hex_address(0x08C3),
            "queue_consumer": hex_address(QUEUE_CONSUMER),
            "command_dispatch": hex_address(COMMAND_DISPATCH),
        },
        "queue": {
            "write_cursor": hex_address(QUEUE_CURSOR),
            "read_cursor": hex_address(QUEUE_READ_CURSOR),
            "data_base": hex_address(QUEUE_DATA),
            "size": QUEUE_SIZE,
            "packet_marker": "0xFF",
        },
        "command_handlers": {
            f"0x{opcode:02X}": hex_address(address)
            for opcode, address in sorted(knowledge_handlers.items())
        },
        "hardware_sites": {
            "ym2612_base_loads": [
                hex_address(offset)
                for offset in find_all(driver, bytes.fromhex("FD 21 00 40"))
            ],
            "ym2612_port0_writes": [
                hex_address(offset)
                for offset in find_all(driver, bytes.fromhex("32 00 40"))
            ],
            "ym2612_port1_writes": [
                hex_address(offset)
                for offset in find_all(driver, bytes.fromhex("32 01 40"))
            ],
            "psg_base_loads": [
                hex_address(offset)
                for offset in find_all(driver, bytes.fromhex("21 11 7F"))
            ],
        },
        "sequence_table": sequence_table_report(image),
        "evidence": {
            "68k_copy_routine": "0x001E573A",
            "queue_trace": "mame_audio_mailbox_queue_trace",
            "command_dispatch_disassembly": "z80_driver_command_dispatch_0945",
        },
    }
    return report, driver


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path, nargs="?", default=ROOT / "rom/Disneys_Aladdin_U_p1.bin")
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "build/re/z80-sound-driver",
        help="directory for the extracted binary and JSON map",
    )
    args = parser.parse_args()
    rom = args.rom.resolve()
    output = args.output.resolve()
    report, driver = build_report(rom)
    output.mkdir(parents=True, exist_ok=True)
    (output / "driver.bin").write_bytes(driver)
    write_json(output / "driver.json", report)
    print(f"z80 driver: {len(driver)} bytes")
    print(f"queue consumer: {report['entry_points']['queue_consumer']}")
    print(f"command dispatch: {report['entry_points']['command_dispatch']}")
    print(f"command handlers: {len(report['command_handlers'])}")
    print(f"output: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
