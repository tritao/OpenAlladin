#!/usr/bin/env python3
"""Resolve common actor initializer records emitted by the MAME debugger."""

from __future__ import annotations

import argparse
import json
import re
import struct
from collections import defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOG = ROOT / "debug.log"
DEFAULT_ROM = ROOT / "Disneys_Aladdin_U_p1.bin"
DEFAULT_OUTPUT = ROOT / "build/re/actor_initializers.json"
ACTOR_TABLE_BASE = 0xFF7E40
ACTOR_STRIDE = 0x42
ACTOR_SLOTS = 32
TEMPLATE_SIZE = 0x42

INIT_RE = re.compile(
    r"OPENALADDIN_ACTOR_INIT"
    r" DEST=(?P<dest>[0-9A-Fa-f]+)"
    r" SOURCE=(?P<source>[0-9A-Fa-f]+)"
    r" PC=(?P<pc>[0-9A-Fa-f]+)"
    r" RETURN=(?P<return>[0-9A-Fa-f]+)"
)


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def hex_address(value: int) -> str:
    return f"0x{value:08X}"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, default=DEFAULT_LOG)
    parser.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rom = args.rom.read_bytes()
    records: list[dict[str, Any]] = []
    by_source: dict[int, dict[str, Any]] = defaultdict(
        lambda: {
            "source": None,
            "count": 0,
            "destinations": set(),
            "slots": set(),
            "callers": set(),
            "breakpoint_pcs": set(),
        }
    )

    for line in args.log.read_text(encoding="utf-8", errors="replace").splitlines():
        match = INIT_RE.search(line)
        if not match:
            continue
        row = {key: int(value, 16) for key, value in match.groupdict().items()}
        row["destination"] = hex_address(row.pop("dest"))
        row["source"] = hex_address(row.pop("source"))
        row["pc"] = hex_address(row["pc"])
        row["return"] = hex_address(row["return"])
        records.append(row)

        source = int(row["source"], 16)
        destination = int(row["destination"], 16)
        entry = by_source[source]
        entry["source"] = source
        entry["count"] += 1
        entry["destinations"].add(destination)
        entry["callers"].add(int(row["return"], 16))
        entry["breakpoint_pcs"].add(int(row["pc"], 16))
        relative = destination - ACTOR_TABLE_BASE
        if 0 <= relative < ACTOR_STRIDE * ACTOR_SLOTS and relative % ACTOR_STRIDE == 0:
            entry["slots"].add(relative // ACTOR_STRIDE)

    templates = []
    invalid_sources = []
    for source, entry in sorted(by_source.items()):
        if source < 0 or source + TEMPLATE_SIZE > len(rom):
            invalid_sources.append(hex_address(source))
            continue
        entry["destinations"] = sorted(hex_address(value) for value in entry["destinations"])
        entry["slots"] = sorted(entry["slots"])
        entry["callers"] = sorted(hex_address(value) for value in entry["callers"])
        entry["breakpoint_pcs"] = sorted(hex_address(value) for value in entry["breakpoint_pcs"])
        entry["source"] = hex_address(source)
        entry["type"] = rom[source]
        entry["animation_pointer"] = hex_address(read_u32(rom, source + 0x20))
        entry["movement_pointer_candidate"] = hex_address(read_u32(rom, source + 0x34))
        entry["template_size"] = TEMPLATE_SIZE
        templates.append(entry)

    result = {
        "format": "openaladdin-actor-initializer-inventory-v1",
        "log": str(args.log.resolve()),
        "rom": {"path": str(args.rom.resolve()), "size": len(rom)},
        "actor_table": {
            "base": hex_address(ACTOR_TABLE_BASE),
            "stride": ACTOR_STRIDE,
            "slot_count": ACTOR_SLOTS,
        },
        "initializer": {
            "address": "0x001AE30A",
            "template_size": TEMPLATE_SIZE,
            "animation_pointer_offset": "0x20",
            "movement_pointer_candidate_offset": "0x34",
        },
        "records": records,
        "templates": templates,
        "invalid_sources": invalid_sources,
    }
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(
        f"resolved {len(records)} initializer records, "
        f"{len(templates)} templates -> {output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
