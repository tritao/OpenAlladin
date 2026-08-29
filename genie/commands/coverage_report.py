"""CLI rendering for the semantic coverage report."""

from __future__ import annotations

import argparse
import json

from genie.data import DataIndex
from genie.ghidra.database import AnalysisDatabase
from genie.profiles import load_profile
from genie.runtime import ROOT, resolve
from genie.semantic_coverage import build_semantic_coverage
from genie.symbols import SymbolStore


def command_coverage_report(args: argparse.Namespace) -> int:
    database_path = resolve(args.database)
    coverage_path = resolve(args.coverage) if args.coverage else None
    index = DataIndex(
        AnalysisDatabase(database_path),
        root=ROOT,
        symbols=SymbolStore(root=ROOT),
        layout_path=resolve(args.layout) if args.layout else None,
        coverage_path=coverage_path,
        animation_path=resolve(args.animation) if args.animation else None,
        movement_path=resolve(args.movement) if args.movement else None,
        providers=load_profile().semantic_providers(),
    )
    report = build_semantic_coverage(
        index.database,
        data_index=index,
        coverage_path=coverage_path,
        root=ROOT,
    )
    if args.json_output:
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0
    _render(report)
    return 0


def _render(report: dict) -> None:
    functions = report["functions"]
    print("Semantic coverage")
    print("Functions")
    print(f"  discovered       {functions['discovered']}")
    print(f"  canonical         {functions['canonical']}")
    print(f"  semantic          {functions['semantic']}")
    print(f"  mechanical        {functions['mechanical']}")
    print(f"  decompiled        {functions['decompiled']}")
    print(f"  runtime_observed  {functions['runtime_observed']}")
    print("ROM objects")
    for kind, value in report["rom_objects"]["by_kind"].items():
        print(
            f"  {kind:<17} {value['canonical']:>4} canonical / {value['total']:>4} discovered "
            f"({value['bounded']:>4} bounded, {value['with_consumers']:>4} consumers)"
        )
    ram = report["ram"]
    print("RAM")
    print(f"  referenced        {ram['referenced']}")
    print(f"  named             {ram['named']}")
    print(f"  typed             {ram['typed']}")
    integrity = report["integrity"]
    print("Integrity")
    print(f"  unowned ROM       {integrity['unowned_rom_ranges']['count']} ranges / {integrity['unowned_rom_ranges']['bytes']} bytes")
    print(f"  overlaps          {integrity['overlapping_semantic_objects']['count']}")
    print(f"  unknown targets   {integrity['unknown_pointer_targets']['count']}")
    print(f"  unbounded streams {len(integrity['known_stream_roots_without_bounded_extents'])}")
    print(f"  streams no users  {len(integrity['bounded_streams_without_consumers'])}")


__all__ = ["command_coverage_report"]
