#!/usr/bin/env python3
"""Correlate RNC uploads with captured VRAM and CRAM palette state."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from genie.common import ROOT
from genie.assets.rnc_runtime import analyze_rnc_runtime


def default_trace() -> Path:
    gameplay = ROOT / "build/re/actor-gameplay"
    if (gameplay / "vdp_vram_frames.bin").exists() and (gameplay / "vdp_vram_frames.bin").stat().st_size:
        return gameplay
    return ROOT / "build/re/traces"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, default=ROOT / "build/assets/rnc")
    parser.add_argument("--trace", type=Path, default=default_trace())
    parser.add_argument("--load-trace", type=Path, help="optional parsed analyze-rnc-load-trace.py report")
    args = parser.parse_args()

    corpus = args.corpus.resolve()
    trace = args.trace.resolve()
    if not (corpus / "loader_analysis.json").is_file():
        raise SystemExit(f"loader analysis not found: {corpus / 'loader_analysis.json'}")
    if not trace.is_dir():
        raise SystemExit(f"trace directory not found: {trace}")
    load_trace = None
    if args.load_trace:
        load_trace_path = args.load_trace.resolve()
        if not load_trace_path.is_file():
            raise SystemExit(f"load trace report not found: {load_trace_path}")
        load_trace = json.loads(load_trace_path.read_text(encoding="utf-8"))
    report = analyze_rnc_runtime(corpus, trace, load_trace=load_trace)
    summary = report["summary"]
    print(f"runtime frames: {report['trace']['frames']}")
    print(f"loader assets: {summary['loader_asset_count']}")
    print(f"exact VRAM matches: {summary['exact_match_count']}")
    print(f"sample VRAM matches: {summary['sample_match_count']}")
    print(f"palette previews: {summary['palette_preview_count']}")
    print(f"dynamic loader events: {summary.get('dynamic_event_count', 0)}")
    print(f"dynamic loads without VRAM match: {summary.get('dynamic_executed_without_vram_match', 0)}")
    print(f"runtime analysis: {corpus / 'runtime_analysis.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
