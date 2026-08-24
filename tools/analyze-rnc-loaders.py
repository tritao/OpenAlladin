#!/usr/bin/env python3
"""Recover RNC upload calls and their Genesis VDP destinations."""

from __future__ import annotations

import argparse
from pathlib import Path

from common import ROOT, hashes
from lib.rnc_loaders import analyze_rnc_loaders


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", nargs="?", type=Path, default=ROOT / "Disneys_Aladdin_U_p1.bin")
    parser.add_argument("--corpus", type=Path, default=ROOT / "build/assets/rnc")
    parser.add_argument("--functions", type=Path, default=ROOT / "build/re/functions.csv")
    args = parser.parse_args()

    rom_path = args.rom.resolve()
    corpus_root = args.corpus.resolve()
    if not rom_path.is_file():
        raise SystemExit(f"ROM not found: {rom_path}")
    if not (corpus_root / "manifest.json").is_file():
        raise SystemExit(f"RNC corpus manifest not found: {corpus_root / 'manifest.json'}")

    report = analyze_rnc_loaders(
        rom_path.read_bytes(),
        corpus_root,
        rom_identity={"path": str(rom_path), **hashes(rom_path)},
        functions_path=args.functions.resolve(),
    )
    summary = report["summary"]
    print(f"loader calls: {summary['call_count']}")
    print(f"RNC uploads: {summary['rnc_loader_call_count']}")
    print(f"resolved VDP destinations: {summary['resolved_destination_count']}")
    print(f"load groups: {summary['load_group_count']}")
    print(f"loader analysis: {corpus_root / 'loader_analysis.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
