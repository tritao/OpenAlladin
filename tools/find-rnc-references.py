#!/usr/bin/env python3
"""Find 68000-style ROM pointers to the RNC asset corpus."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from common import ROOT, hashes
from lib.rnc_refs import scan_rnc_references, write_rnc_references


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", nargs="?", type=Path, default=ROOT / "Disneys_Aladdin_U_p1.bin")
    parser.add_argument("--corpus", type=Path, default=ROOT / "build/assets/rnc/manifest.json")
    parser.add_argument("--output", type=Path, default=ROOT / "build/assets/rnc/pointer_references.json")
    args = parser.parse_args()

    rom_path = args.rom.resolve()
    corpus_path = args.corpus.resolve()
    if not rom_path.is_file():
        raise SystemExit(f"ROM not found: {rom_path}")
    if not corpus_path.is_file():
        raise SystemExit(f"RNC corpus manifest not found: {corpus_path}")

    rom_data = rom_path.read_bytes()
    corpus = json.loads(corpus_path.read_text(encoding="utf-8"))
    identity = {"path": str(rom_path), **hashes(rom_path)}
    corpus_identity = corpus.get("rom", {})
    for key in ("size", "crc32", "sha1", "sha256"):
        if corpus_identity.get(key) != identity[key]:
            raise SystemExit(
                f"corpus ROM {key} mismatch: "
                f"manifest={corpus_identity.get(key)} actual={identity[key]}"
            )

    result = scan_rnc_references(rom_data, corpus)
    result["rom"] = identity
    result["corpus"] = str(corpus_path)
    write_rnc_references(args.output.resolve(), result)

    print(f"RNC targets: {result['target_count']}")
    print(f"pointer references: {result['reference_count']}")
    print(f"unassigned references: {result['unassigned_reference_count']}")
    print(f"pointer tables: {result['pointer_table_count']}")
    print(f"report: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
