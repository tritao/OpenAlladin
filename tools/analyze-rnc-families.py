#!/usr/bin/env python3
"""Group unassigned RNC assets by storage and ROM code evidence."""

from __future__ import annotations

import argparse
from pathlib import Path

from common import ROOT, hashes
from lib.rnc_families import CODE_CLUSTER_GAP, analyze_rnc_families


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", nargs="?", type=Path, default=ROOT / "Disneys_Aladdin_U_p1.bin")
    parser.add_argument("--corpus", type=Path, default=ROOT / "build/assets/rnc")
    parser.add_argument(
        "--functions",
        type=Path,
        default=ROOT / "build/re/functions.csv",
        help="optional Ghidra function export used to label code clusters",
    )
    parser.add_argument(
        "--code-cluster-gap",
        type=lambda value: int(value, 0),
        default=CODE_CLUSTER_GAP,
        help="maximum gap between code references in one cluster (default: 0x100)",
    )
    args = parser.parse_args()

    rom_path = args.rom.resolve()
    corpus_root = args.corpus.resolve()
    if not rom_path.is_file():
        raise SystemExit(f"ROM not found: {rom_path}")
    for filename in ("manifest.json", "classification.json", "pointer_references.json"):
        if not (corpus_root / filename).is_file():
            raise SystemExit(f"RNC report not found: {corpus_root / filename}")

    identity = {"path": str(rom_path), **hashes(rom_path)}
    report = analyze_rnc_families(
        rom_path.read_bytes(),
        corpus_root,
        rom_identity=identity,
        functions_path=args.functions.resolve(),
        code_cluster_gap=args.code_cluster_gap,
    )
    summary = report["summary"]
    print(f"unassigned RNC blocks: {summary['unassigned_block_count']}")
    print(f"storage families: {summary['storage_family_count']}")
    print(f"code clusters: {summary['code_cluster_count']}")
    print(f"code references: {summary['reference_count']}")
    print(f"family analysis: {corpus_root / 'family_analysis.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
