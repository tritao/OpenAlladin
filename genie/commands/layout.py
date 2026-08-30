"""Command-line boundary for ROM layout classification."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from genie.ghidra.database import AnalysisDatabase, render_records
from genie.layout import Layout, build_layout, build_layout_candidates, validate_layout
from genie.runtime import ROOT, default_rom, resolve


DEFAULT_DATABASE = ROOT / "build/re/full-rom"


def _database(path: Path) -> AnalysisDatabase:
    database = AnalysisDatabase(resolve(path))
    try:
        database.load("metadata.json")
    except (OSError, ValueError, TypeError) as error:
        raise SystemExit(str(error)) from error
    return database


def _load_layout(path: Path) -> Layout:
    source = resolve(path)
    try:
        value = json.loads(source.read_text(encoding="utf-8"))
        return Layout.from_dict(value)
    except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError) as error:
        raise SystemExit(f"could not read layout {source}: {error}") from error


def _render_range(item, *, json_output: bool) -> int:
    if json_output:
        print(json.dumps(item.to_dict(), indent=2, sort_keys=True))
    else:
        print(f"start       0x{item.start:08X}")
        print(f"end         0x{item.end:08X}")
        print(f"size        {item.size}")
        print(f"class       {item.layout_class}")
        print(f"source      {item.source}")
        if item.name:
            print(f"name        {item.name}")
    return 0


def command_layout_build(args: argparse.Namespace) -> int:
    database_path = resolve(args.database)
    database = _database(args.database)
    layout = build_layout(database, root=ROOT, include_artifacts=not args.no_artifacts)
    output = resolve(args.output) if args.output else database_path / "layout.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(layout.to_dict(), indent=2, sort_keys=True) + "\n", encoding="utf-8")
    errors = validate_layout(layout)
    if errors:
        for error in errors:
            print(f"ERROR {error}")
        return 1
    print(f"Built ROM layout: {len(layout.ranges)} ranges -> {output}")
    return 0


def command_layout_show(args: argparse.Namespace) -> int:
    item = _load_layout(args.layout).at(args.address)
    if item is None:
        print(f"No layout range contains 0x{args.address:08X}")
        return 1
    return _render_range(item, json_output=args.json_output)


def command_layout_gaps(args: argparse.Namespace) -> int:
    layout = _load_layout(args.layout)
    ranges = layout.gaps()
    if args.json_output:
        print(json.dumps([item.to_dict() for item in ranges], indent=2, sort_keys=True))
    else:
        render_records([item.to_dict() for item in ranges], json_output=False)
    return 0


def command_layout_stats(args: argparse.Namespace) -> int:
    layout = _load_layout(args.layout)
    document = layout.to_dict()
    if args.json_output:
        print(json.dumps({
            "rom_size": layout.rom_size,
            "ranges": len(layout.ranges),
            "counts": document["counts"],
            "bytes": document["bytes"],
        }, indent=2, sort_keys=True))
    else:
        print(f"ROM bytes  {layout.rom_size}")
        print(f"Ranges     {len(layout.ranges)}")
        for class_name, count in document["counts"].items():
            print(f"{class_name:<18} {count:>8} ranges  {document['bytes'][class_name]:>10} bytes")
    return 0


def command_layout_validate(args: argparse.Namespace) -> int:
    errors = validate_layout(_load_layout(args.layout))
    if args.json_output:
        print(json.dumps({"valid": not errors, "errors": errors}, indent=2, sort_keys=True))
    elif errors:
        for error in errors:
            print(f"ERROR {error}")
    else:
        print("Validated ROM layout coverage")
    return 1 if errors else 0


def command_layout_candidates(args: argparse.Namespace) -> int:
    database = _database(args.database)
    layout = _load_layout(args.layout)
    rom_path = resolve(args.rom)
    rom = rom_path.read_bytes() if rom_path.is_file() else None
    items = build_layout_candidates(
        database,
        layout,
        root=ROOT,
        rom=rom,
        animation_path=resolve(args.animation) if args.animation else None,
        movement_path=resolve(args.movement) if args.movement else None,
        max_references=args.max_references,
    )
    limit = args.limit if args.limit > 0 else len(items)
    selected = items[:limit]
    if args.json_output:
        print(json.dumps({"total": len(items), "items": selected}, indent=2, sort_keys=True))
        return 0
    print(f"ROM layout candidates ({len(selected)} of {len(items)})")
    if not selected:
        return 0
    print("rank score gap                         class                         confidence evidence")
    for item in selected:
        gap = item["gap"]
        counts = item["evidence_counts"]
        evidence = (
            f"{counts['actor_template_pointers']} template, "
            f"{counts['decoded_streams']} decoded, "
            f"{counts['vm_probes']} probes, "
            f"{counts['boundary_conflicts']} boundary conflicts, "
            f"{counts['direct_references']} refs "
            f"({counts['code_backed_references']} code, "
            f"{counts['data_only_references']} data-only)"
        )
        print(
            f"{item['rank']:>4} {item['score']:>5} "
            f"{gap['start']}-{gap['end']} {item['suggested_class']:<29} "
            f"{item['confidence']:<10} {evidence}"
        )
    return 0


__all__ = [name for name in globals() if not name.startswith("__")]
