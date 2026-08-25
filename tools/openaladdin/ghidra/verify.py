#!/usr/bin/env python3
"""Print ROM identity and reject unexpected images unless explicitly allowed."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from openaladdin.common import ROOT, hashes, rom_entries


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path, nargs="?", help="ROM path")
    parser.add_argument(
        "--allow-unverified",
        action="store_true",
        help="accept a ROM whose identity is not in re/config/roms.yml",
    )
    args = parser.parse_args()

    default_name, expected, _ = rom_entries()
    path = args.rom or ROOT / str(expected.get("expected_filename", "rom/Disneys_Aladdin_U_p1.bin"))
    if not path.is_absolute():
        path = Path.cwd() / path
    path = path.resolve()
    if not path.is_file():
        print(f"ROM not found: {path}", file=sys.stderr)
        return 2

    actual = hashes(path)
    print(expected.get("description", default_name))
    print()
    for key, label in (("size", "size"), ("crc32", "CRC32"), ("sha1", "SHA1"), ("sha256", "SHA256")):
        configured = str(expected.get(key, "")).upper()
        actual_value = str(actual[key]).upper()
        match = configured == actual_value if configured else False
        state = "OK" if match else "MISMATCH"
        print(f"{label:<7} {actual[key]}  {state}")

    expected_values = {key: str(expected.get(key, "")).upper() for key in actual}
    known = all(expected_values[key] and expected_values[key] == str(actual[key]).upper() for key in actual)
    if not known and not args.allow_unverified:
        print("\nROM rejected. Use --allow-unverified for exploratory work.", file=sys.stderr)
        return 1
    if not known:
        print("\nROM accepted with --allow-unverified.")
    else:
        print("\nROM accepted.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
