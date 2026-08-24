#!/usr/bin/env python3
"""Extract native Genesis assets from a local Aladdin ROM image."""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import sys
from typing import Any

from common import ROOT, hashes
from lib.chopper import extract_chopper
from lib.levels import extract_levels, find_level_table, read_level_table
from lib.rnc import extract_rnc_corpus, is_rnc, parse_header
from lib.rnc_assets import classify_rnc_corpus
from lib.rnc_refs import scan_rnc_references, write_rnc_references


def _animation_module():
    path = ROOT / "tools/decode-animation-streams.py"
    if not path.exists():
        return None
    spec = importlib.util.spec_from_file_location("openaladdin_animation_decoder", path)
    if spec is None or spec.loader is None:
        return None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def extract_animation(data: bytes, output: Path) -> dict[str, Any] | None:
    module = _animation_module()
    if module is None:
        return None
    rom = module.RomReader(data)
    decoder = module.AnimationDecoder(rom)
    streams = {
        name: decoder.decode_stream(address, 512, 8192, False)
        for name, address in module.PLAYER_STREAMS.items()
    }
    result = {
        "rom_size": len(data),
        "vm": {
            "command_range": ["0xEA", "0xFE"],
            "dispatch_table": f"0x{module.VM_DISPATCH_TABLE:06X}",
            "dispatch": module.build_dispatch_table(rom),
        },
        "streams": streams,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return {
        "file": str(output.name),
        "stream_count": len(streams),
        "instruction_count": sum(len(stream["instructions"]) for stream in streams.values()),
    }


def inventory_rnc(data: bytes) -> list[dict[str, Any]]:
    result = []
    offset = 0
    while True:
        offset = data.find(b"RNC\x01", offset)
        if offset < 0:
            break
        try:
            header = parse_header(data, offset)
            result.append({
                "offset": f"0x{offset:06X}",
                "method": header.method,
                "compressed_bytes": header.total_size,
                "decompressed_bytes": header.unpacked_size,
                "end": f"0x{offset + header.total_size:06X}",
            })
        except ValueError as error:
            result.append({"offset": f"0x{offset:06X}", "error": str(error)})
        offset += 4
    return result


def level_rnc_references(data: bytes) -> dict[int, list[str]]:
    """Associate known level-table RNC pointers with human-readable consumers."""

    references: dict[int, list[str]] = {}
    try:
        table_offset = find_level_table(data)
        table = read_level_table(data, table_offset)
    except ValueError:
        return references

    for index, (_, entry, _) in enumerate(table):
        for field in ("floor", "chars", "map", "parallax"):
            address = int(getattr(entry, field))
            if address and is_rnc(data, address):
                references.setdefault(address, []).append(f"level{index:02d}.{field}")
    return references


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", nargs="?", type=Path, help="raw Genesis ROM; defaults to the configured local dump")
    parser.add_argument("--output", type=Path, default=ROOT / "build/assets", help="generated asset directory")
    parser.add_argument("--no-levels", action="store_true", help="skip level extraction")
    parser.add_argument("--no-sprites", action="store_true", help="skip Chopper sprite extraction")
    parser.add_argument("--no-animations", action="store_true", help="skip animation stream extraction")
    return parser.parse_args()


def default_rom() -> Path:
    configured = ROOT / "Disneys_Aladdin_U_p1.bin"
    if configured.exists():
        return configured
    return ROOT / "rom/aladdin-usa.bin"


def main() -> int:
    args = parse_args()
    rom_path = (args.rom or default_rom()).resolve()
    if not rom_path.exists():
        raise SystemExit(f"ROM not found: {rom_path}")
    data = rom_path.read_bytes()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    rom_identity = {"path": str(rom_path), **hashes(rom_path)}
    manifest: dict[str, Any] = {
        "format": "openaladdin-assets-v1",
        "rom": rom_identity,
        "pipeline": {
            "levels": not args.no_levels,
            "sprites": not args.no_sprites,
            "animations": not args.no_animations,
        },
        "inventory": {"rnc_blocks": inventory_rnc(data)},
        "assets": {},
        "warnings": [],
    }

    try:
        rnc_result = extract_rnc_corpus(
            data,
            output / "rnc",
            references=level_rnc_references(data),
            rom_identity=rom_identity,
        )
        manifest["inventory"]["rnc_blocks"] = rnc_result["blocks"]
        manifest["assets"]["rnc"] = {
            "file": "rnc/manifest.json",
            "block_count": rnc_result["block_count"],
            "decoded_count": rnc_result["decoded_count"],
            "assigned_count": rnc_result["assigned_count"],
            "unassigned_count": rnc_result["unassigned_count"],
        }
        classification = classify_rnc_corpus(output / "rnc")
        manifest["assets"]["rnc"].update({
            "classification": "rnc/classification.json",
            "tile_candidates": classification["tile_candidates"],
            "families": len(classification["families"]),
        })
        pointer_references = scan_rnc_references(data, rnc_result)
        pointer_references.update({
            "rom": rom_identity,
            "corpus": "rnc/manifest.json",
        })
        write_rnc_references(output / "rnc/pointer_references.json", pointer_references)
        manifest["assets"]["rnc"].update({
            "pointer_references": "rnc/pointer_references.json",
            "pointer_reference_count": pointer_references["reference_count"],
            "pointer_tables": pointer_references["pointer_table_count"],
        })
    except (OSError, ValueError) as error:
        manifest["warnings"].append(f"rnc: {error}")

    if not args.no_levels:
        try:
            level_result = extract_levels(data, output / "levels")
            (output / "levels.json").write_text(json.dumps(level_result, indent=2) + "\n", encoding="utf-8")
            manifest["assets"]["levels"] = {
                "file": "levels.json",
                "count": level_result["count"],
                "table_offset": level_result["table_offset"],
            }
        except (ValueError, OSError) as error:
            manifest["warnings"].append(f"levels: {error}")

    if not args.no_sprites:
        try:
            sprite_result = extract_chopper(data, output / "sprites")
            sprite_summary = {
                "supported": sprite_result.get("supported", False),
                "pointer_table": sprite_result.get("pointer_table"),
                "frame_struct_size": sprite_result.get("frame_struct_size"),
                "frame_count": sprite_result.get("frame_count", 0),
                "tile_sets": sprite_result.get("tile_sets", {}),
                "details": "sprites/frames.json",
            }
            (output / "sprites.json").write_text(json.dumps(sprite_summary, indent=2) + "\n", encoding="utf-8")
            manifest["assets"]["sprites"] = {
                "file": "sprites.json",
                "details": "sprites/frames.json",
                "supported": sprite_summary["supported"],
                "frame_count": sprite_summary["frame_count"],
            }
        except (ValueError, OSError) as error:
            manifest["warnings"].append(f"sprites: {error}")

    if not args.no_animations:
        animation_result = extract_animation(data, output / "animations.json")
        if animation_result is not None:
            manifest["assets"]["animations"] = animation_result
        else:
            manifest["warnings"].append("animations: decoder script is not present")

    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"ROM: {rom_path}")
    print(f"RNC blocks inventoried: {len(manifest['inventory']['rnc_blocks'])}")
    for name, value in manifest["assets"].items():
        print(f"{name}: {value}")
    for warning in manifest["warnings"]:
        print(f"warning: {warning}", file=sys.stderr)
    print(f"asset manifest: {output / 'manifest.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
