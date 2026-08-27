#!/usr/bin/env python3
"""Classify and render unassigned blocks from the RNC asset corpus."""

from __future__ import annotations

import argparse
from pathlib import Path

from genie.common import ROOT
from genie.assets.rnc_assets import classify_rnc_corpus


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, default=ROOT / "build/assets/rnc")
    args = parser.parse_args()

    result = classify_rnc_corpus(args.corpus.resolve())
    print(f"unassigned blocks: {result['unassigned_blocks']}")
    print(f"Genesis tile candidates: {result['tile_candidates']}")
    print(f"unclassified binary candidates: {result['binary_candidates']}")
    print(f"contiguous families: {len(result['families'])}")
    print(f"classification: {args.corpus.resolve() / 'classification.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
