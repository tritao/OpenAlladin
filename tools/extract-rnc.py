#!/usr/bin/env python3
"""Decode and catalog every RNC block in the local Aladdin ROM."""

from __future__ import annotations

import argparse
from pathlib import Path

from common import ROOT, hashes
from lib.levels import find_level_table, read_level_table
from lib.rnc import extract_rnc_corpus, is_rnc


def default_rom() -> Path:
    configured = ROOT / "Disneys_Aladdin_U_p1.bin"
    if configured.exists():
        return configured
    return ROOT / "rom/aladdin-usa.bin"


def level_references(data: bytes) -> dict[int, list[str]]:
    references: dict[int, list[str]] = {}
    try:
        table_offset = find_level_table(data)
        table = read_level_table(data, table_offset)
    except ValueError:
        return references

    for index, (_, entry, _) in enumerate(table):
        for field in ("floor", "chars", "map", "parallax"):
            address = int(getattr(entry, field))
            if address and is_rnc(data, address):
                references.setdefault(address, []).append(f"level{index:02d}.{field}")
    return references


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", nargs="?", type=Path, default=default_rom())
    parser.add_argument("--output", type=Path, default=ROOT / "build/assets")
    args = parser.parse_args()

    rom_path = args.rom.resolve()
    if not rom_path.is_file():
        raise SystemExit(f"ROM not found: {rom_path}")
    data = rom_path.read_bytes()
    identity = {"path": str(rom_path), **hashes(rom_path)}
    result = extract_rnc_corpus(
        data,
        args.output.resolve() / "rnc",
        references=level_references(data),
        rom_identity=identity,
    )

    print(f"ROM: {rom_path}")
    print(f"RNC blocks: {result['block_count']}")
    print(f"decoded: {result['decoded_count']}")
    print(f"assigned: {result['assigned_count']}")
    print(f"unassigned: {result['unassigned_count']}")
    print(f"RNC manifest: {args.output.resolve() / 'rnc/manifest.json'}")
    return 0 if result["failed_count"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
