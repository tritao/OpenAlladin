"""Command-line boundary for semantic ROM-data queries."""

from __future__ import annotations

import argparse
import json

from genie.data import DataIndex
from genie.ghidra.database import AnalysisDatabase
from genie.runtime import ROOT, resolve
from genie.symbols import SymbolStore


DATA_KINDS = (
    "animation",
    "movement",
    "actor-template",
    "pointer-table",
    "rom-data",
    "all",
)


def _index(args: argparse.Namespace) -> DataIndex:
    database_path = resolve(args.database)
    return DataIndex(
        AnalysisDatabase(database_path),
        root=ROOT,
        symbols=SymbolStore(root=ROOT),
        layout_path=resolve(args.layout) if args.layout else None,
        coverage_path=resolve(args.coverage) if getattr(args, "coverage", None) else None,
        animation_path=resolve(args.animation) if getattr(args, "animation", None) else None,
        movement_path=resolve(args.movement) if getattr(args, "movement", None) else None,
    )


def _render_todo(items: list[dict], *, total: int, json_output: bool, title: str) -> None:
    if json_output:
        print(json.dumps({"total": total, "items": items}, indent=2, sort_keys=True))
        return
    print(f"{title} ({len(items)} of {total})")
    if not items:
        return
    print("rank score address       kind           name                              size reasons")
    for item in items:
        reasons = ",".join(item["reasons"]) or "none"
        print(
            f"{item['rank']:>4} {item['score']:>5} {item['address']} "
            f"{item['kind']:<14} {item['name'][:32]:<32} {item['size']:>5} {reasons}"
        )


def command_data_stats(args: argparse.Namespace) -> int:
    value = _index(args).stats()
    if args.json_output:
        print(json.dumps(value, indent=2, sort_keys=True))
        return 0
    print(f"ROM objects       {value['objects']}")
    for kind, count in value["by_kind"].items():
        print(f"{kind:<18} {count:>6}")
    print(f"range-bounded     {value['ranged']}")
    print(f"decoded           {value['decoded']}")
    print(f"with consumers    {value['with_consumers']}")
    print(f"runtime observed  {value['runtime_observed']}")
    print(f"overlapping       {value['overlapping_objects']}")
    print(f"unowned ROM       {value['unowned_rom_ranges']} ranges / {value['unowned_rom_bytes']} bytes")
    ram = value["ram"]
    print(f"RAM               {ram['referenced']} referenced / {ram['named']} named / {ram['typed']} typed")
    return 0


def command_data_todo(args: argparse.Namespace) -> int:
    items = _index(args).todo(kind=args.kind)
    limit = args.limit if args.limit > 0 else len(items)
    _render_todo(items[:limit], total=len(items), json_output=args.json_output, title="Data work queue")
    return 0


def command_data_next(args: argparse.Namespace) -> int:
    items = _index(args).todo(kind=args.kind)
    if not items:
        print("No data work items remain")
        return 1
    _render_todo(items[:1], total=len(items), json_output=args.json_output, title="Next data object")
    return 0


def _render_context(value: dict, *, json_output: bool) -> int:
    if json_output:
        print(json.dumps(value, indent=2, sort_keys=True))
        return 0
    item = value["object"]
    print(item["name"])
    print(f"{item['address']}..{item['end']}  {item['size']} bytes")
    print(f"kind: {item['kind']}")
    print(f"confidence: {item['confidence']}")
    print("consumers:")
    for consumer in value["consumers"]:
        address = f" ({consumer['address']})" if consumer["address"] else ""
        print(f"  {consumer['name']}{address} [{consumer['references']} references]")
    print("references:")
    for reference in value["references"]:
        source = reference.get("from_function_name") or reference.get("from") or "<unknown>"
        print(f"  {source} -> {reference.get('to')} [{reference.get('type', '')}]")
    print("outgoing stream refs:")
    for reference in value["outgoing_stream_refs"]:
        print(f"  {reference['name']} ({reference['address']})")
    decoder = value.get("decoder")
    if decoder and decoder.get("available"):
        suffix = "" if decoder.get("size_matches") else " (size mismatch)"
        print(f"decoded: yes ({decoder.get('bytes_decoded', 0)} bytes){suffix}")
    elif decoder is not None:
        print("decoded: no")
    else:
        print("decoded: n/a")
    print(f"range bounded: {'yes' if value['range_bounded'] else 'no'}")
    if value["overlap"]:
        print("overlap:")
        for other in value["overlap"]:
            print(f"  {other['name']} {other['start']}..{other['end']}")
    else:
        print("overlap: none")
    runtime = value["runtime"]
    print(f"runtime observed: {'yes' if runtime.get('observed') else 'no'}")
    if runtime.get("scenarios"):
        print(f"runtime scenarios: {', '.join(runtime['scenarios'])}")
    return 0


def command_data_context(args: argparse.Namespace) -> int:
    value = _index(args).context(args.address)
    if value is None:
        print(f"No data object contains 0x{args.address:08X}")
        return 1
    return _render_context(value, json_output=args.json_output)


__all__ = [
    "DATA_KINDS",
    "command_data_context",
    "command_data_next",
    "command_data_stats",
    "command_data_todo",
]
