"""Recover RNC-to-VDP upload call sites from the 68000 ROM."""

from __future__ import annotations

import bisect
import csv
import json
from pathlib import Path
import struct
from typing import Any


LOADER_ADDRESS = 0x1B3416
MAX_SOURCE_LOOKBACK = 0x30


def _address(value: Any) -> int:
    return int(str(value), 16)


def _hex(value: int) -> str:
    return f"0x{value:06X}"


def _function_index(path: Path | None) -> tuple[list[int], dict[int, str]]:
    if path is None or not path.is_file():
        return [], {}
    starts: list[int] = []
    names: dict[int, str] = {}
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            try:
                address = _address(row["address"])
            except (KeyError, TypeError, ValueError):
                continue
            starts.append(address)
            names[address] = row.get("name") or f"FUN_{address:06x}"
    starts = sorted(set(starts))
    return starts, names


def _containing_function(address: int, starts: list[int], names: dict[int, str]) -> dict[str, str] | None:
    if not starts:
        return None
    index = bisect.bisect_right(starts, address) - 1
    if index < 0:
        return None
    start = starts[index]
    return {"address": _hex(start), "name": names[start]}


def _decode_lea(data: bytes, address: int, register: int | None = None) -> dict[str, Any] | None:
    if address < 0 or address + 2 > len(data):
        return None
    opcode = struct.unpack_from(">H", data, address)[0]
    actual_register = (opcode >> 9) & 7
    if register is not None and actual_register != register:
        return None
    if opcode & 0xF1FF == 0x41F9 and address + 6 <= len(data):
        return {
            "address": _hex(address),
            "register": f"A{actual_register}",
            "mode": "absolute_long",
            "target": struct.unpack_from(">I", data, address + 2)[0],
            "size": 6,
            "bytes": data[address:address + 6].hex().upper(),
        }
    if opcode & 0xF1FF == 0x41F8 and address + 4 <= len(data):
        target = struct.unpack_from(">h", data, address + 2)[0] & 0xFFFFFFFF
        return {
            "address": _hex(address),
            "register": f"A{actual_register}",
            "mode": "absolute_word",
            "target": target,
            "size": 4,
            "bytes": data[address:address + 4].hex().upper(),
        }
    return None


def _loader_calls(data: bytes) -> list[int]:
    calls = []
    for address in range(0, max(0, len(data) - 3), 2):
        opcode, displacement = struct.unpack_from(">Hh", data, address)
        if opcode != 0x6100:
            continue
        # For a word-displacement 68000 branch, the extension-word address is
        # the PC-relative base.
        if address + 2 + displacement == LOADER_ADDRESS:
            calls.append(address)
    return calls


def _find_previous_lea(
    data: bytes,
    address: int,
    register: int,
    maximum_distance: int = MAX_SOURCE_LOOKBACK,
) -> dict[str, Any] | None:
    start = max(0, address - maximum_distance)
    for candidate in range(address - 2, start - 1, -2):
        lea = _decode_lea(data, candidate, register)
        if lea is not None:
            lea["distance"] = address - candidate
            return lea
    return None


def _rnc_block_map(corpus: dict[str, Any]) -> dict[int, dict[str, Any]]:
    return {
        _address(block["offset"]): block
        for block in corpus.get("blocks", [])
        if block.get("decoded")
    }


def analyze_rnc_loaders(
    rom_data: bytes,
    corpus_root: Path,
    rom_identity: dict[str, Any] | None = None,
    functions_path: Path | None = None,
) -> dict[str, Any]:
    """Recover every direct call to the game's RNC-to-VDP upload helper."""

    corpus_root = corpus_root.resolve()
    corpus = json.loads((corpus_root / "manifest.json").read_text(encoding="utf-8"))
    classification_path = corpus_root / "classification.json"
    classification = json.loads(classification_path.read_text(encoding="utf-8")) if classification_path.exists() else {}
    classification_by_offset = {
        _address(block["offset"]): block
        for block in classification.get("blocks", [])
    }
    family_analysis_path = corpus_root / "family_analysis.json"
    family_analysis = json.loads(family_analysis_path.read_text(encoding="utf-8")) if family_analysis_path.exists() else {}
    family_by_offset = {
        _address(offset): family["id"]
        for family in family_analysis.get("storage_families", [])
        for offset in family.get("blocks", [])
    }
    blocks = _rnc_block_map(corpus)
    starts, names = _function_index(functions_path)
    calls: list[dict[str, Any]] = []

    for call_address in _loader_calls(rom_data):
        destination = _find_previous_lea(rom_data, call_address, 1, maximum_distance=8)
        source = _find_previous_lea(rom_data, call_address, 0)
        row: dict[str, Any] = {
            "call_address": _hex(call_address),
            "loader_address": _hex(LOADER_ADDRESS),
            "function": _containing_function(call_address, starts, names),
            "source": None,
            "destination": None,
            "block": None,
            "status": "unresolved_source",
        }
        if source is not None:
            source_target = source["target"]
            row["source"] = {
                **source,
                "target": _hex(source_target),
                "resolution": "backtracked_a0" if source["distance"] > 12 else "nearby_a0",
            }
            block = blocks.get(source_target)
            if block is not None:
                row["block"] = {
                    "offset": block["offset"],
                    "packed_bytes": block.get("packed_bytes"),
                    "unpacked_bytes": block.get("unpacked_bytes"),
                    "sha1": block.get("sha1"),
                    "classification": classification_by_offset.get(source_target, {}).get("classification"),
                    "preview": (classification_by_offset.get(source_target, {}).get("rendered") or {}).get("file"),
                    "storage_family": family_by_offset.get(source_target),
                }
                row["status"] = "resolved_rnc"
            else:
                row["status"] = "non_rnc_source"
        if destination is not None:
            destination_target = destination["target"]
            row["destination"] = {
                **destination,
                "target": _hex(destination_target),
                "space": "vdp_vram",
                "resolution": "immediate_a1" if destination["distance"] <= 6 else "backtracked_a1",
            }
        if row["block"] is not None and row["destination"] is not None:
            unpacked_bytes = int(row["block"].get("unpacked_bytes") or 0)
            vdp_address = int(destination["target"])
            row["upload"] = {
                "destination": _hex(vdp_address),
                "bytes": unpacked_bytes,
                "words": unpacked_bytes // 2 if unpacked_bytes % 2 == 0 else None,
                "end_exclusive": _hex((vdp_address + unpacked_bytes) & 0x3FFF),
                "tile_count": unpacked_bytes // 32 if unpacked_bytes % 32 == 0 else None,
            }
        calls.append(row)

    resolved = [row for row in calls if row["status"] == "resolved_rnc"]
    groups: dict[str, list[dict[str, Any]]] = {}
    for row in resolved:
        function = row.get("function") or {"address": "unknown", "name": "unknown"}
        groups.setdefault(function["name"], []).append(row)
    load_groups = []
    for name, group in sorted(groups.items()):
        load_groups.append({
            "name": name,
            "function": group[0].get("function"),
            "call_count": len(group),
            "blocks": sorted({row["block"]["offset"] for row in group}),
            "destinations": sorted({row["destination"]["target"] for row in group if row.get("destination")}),
            "calls": group,
        })

    by_destination: dict[str, int] = {}
    for row in resolved:
        if row.get("destination"):
            target = row["destination"]["target"]
            by_destination[target] = by_destination.get(target, 0) + 1
    report = {
        "format": "openaladdin-rnc-loader-analysis-v1",
        "rom": rom_identity or corpus.get("rom", {}),
        "inputs": {
            "corpus": "rnc/manifest.json",
            "classification": "rnc/classification.json" if classification_path.exists() else None,
            "family_analysis": "rnc/family_analysis.json" if family_analysis_path.exists() else None,
            "functions": "build/re/functions.csv" if functions_path and functions_path.is_file() else None,
        },
        "loader": {
            "address": _hex(LOADER_ADDRESS),
            "semantics": "A0 points to an RNC method-1 block; A1 is a VDP VRAM byte address; the helper emits the VDP command and streams decompressed bytes.",
        },
        "summary": {
            "call_count": len(calls),
            "rnc_loader_call_count": len(resolved),
            "resolved_destination_count": sum(row.get("destination") is not None for row in resolved),
            "unresolved_source_count": sum(row["status"] != "resolved_rnc" for row in calls),
            "by_destination": dict(sorted(by_destination.items())),
            "load_group_count": len(load_groups),
        },
        "load_groups": load_groups,
        "calls": calls,
    }
    output_path = corpus_root / "loader_analysis.json"
    output_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report
