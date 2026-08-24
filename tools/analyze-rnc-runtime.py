#!/usr/bin/env python3
"""Correlate RNC uploads with captured VRAM and CRAM palette state."""

from __future__ import annotations

import argparse
from pathlib import Path

from common import ROOT
from lib.rnc_runtime import analyze_rnc_runtime


def default_trace() -> Path:
    gameplay = ROOT / "build/re/actor-gameplay"
    if (gameplay / "vdp_vram_frames.bin").exists() and (gameplay / "vdp_vram_frames.bin").stat().st_size:
        return gameplay
    return ROOT / "build/re/traces"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, default=ROOT / "build/assets/rnc")
    parser.add_argument("--trace", type=Path, default=default_trace())
    args = parser.parse_args()

    corpus = args.corpus.resolve()
    trace = args.trace.resolve()
    if not (corpus / "loader_analysis.json").is_file():
        raise SystemExit(f"loader analysis not found: {corpus / 'loader_analysis.json'}")
    if not trace.is_dir():
        raise SystemExit(f"trace directory not found: {trace}")
    report = analyze_rnc_runtime(corpus, trace)
    summary = report["summary"]
    print(f"runtime frames: {report['trace']['frames']}")
    print(f"loader assets: {summary['loader_asset_count']}")
    print(f"exact VRAM matches: {summary['exact_match_count']}")
    print(f"sample VRAM matches: {summary['sample_match_count']}")
    print(f"palette previews: {summary['palette_preview_count']}")
    print(f"runtime analysis: {corpus / 'runtime_analysis.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
