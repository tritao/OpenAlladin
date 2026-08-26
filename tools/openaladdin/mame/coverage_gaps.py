#!/usr/bin/env python3
"""Report unobserved entries in the ROM's indirect-dispatch tables."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
from typing import Any

from openaladdin.common import ROOT, hashes, normalize_symbols, write_json


FORMAT = "openaladdin-runtime-coverage-gaps-v1"
SUPPORTED_COVERAGE = {
    "openaladdin-runtime-coverage-v1",
    "openaladdin-runtime-coverage-v2",
}

TABLE_SPECS = (
    ("terrain", "TERRAIN_RESPONSE_HANDLER_TABLE", 256),
    ("actor_vm", "ACTOR_VM_DISPATCH_TABLE", 21),
    ("player_collision", "PLAYER_COLLISION_HANDLER_TABLE", 256),
    ("actor_collision", "ACTOR_COLLISION_HANDLER_TABLE", 256),
    ("interaction", "INTERACTION_HANDLER_TABLE", 256),
)


def _load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ValueError(f"{path}: invalid JSON: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"{path}: expected a JSON object")
    return value


def _hex(value: int) -> str:
    return f"0x{value:06X}"


def build_gap_report(
    coverage: dict[str, Any],
    rom: Path,
    *,
    table_specs: tuple[tuple[str, str, int], ...] = TABLE_SPECS,
    symbol_addresses: dict[str, int] | None = None,
) -> dict[str, Any]:
    if coverage.get("format") not in SUPPORTED_COVERAGE:
        raise ValueError(f"unsupported coverage format: {coverage.get('format')!r}")
    if not rom.is_file():
        raise ValueError(f"ROM not found: {rom}")

    actual_hashes = hashes(rom)
    expected_sha256 = str(coverage.get("rom_sha256") or "")
    if expected_sha256 and expected_sha256 != actual_hashes["sha256"]:
        raise ValueError(
            f"coverage ROM mismatch: report={expected_sha256}, ROM={actual_hashes['sha256']}"
        )

    symbols = symbol_addresses or {
        str(item["name"]): int(item["address"]) for item in normalize_symbols()
    }
    observed: dict[str, dict[int, set[str]]] = {name: {} for name, _, _ in table_specs}
    for edge in coverage.get("edges", []):
        target = int(str(edge["target"]), 0)
        scenarios = set(str(value) for value in edge.get("scenarios", []))
        for table in edge.get("tables", []):
            if table in observed:
                observed[table].setdefault(target, set()).update(scenarios)

    tables = []
    valid_entry_count = 0
    covered_entry_count = 0
    uncovered_entry_count = 0
    invalid_entry_count = 0
    for name, symbol_name, count in table_specs:
        if symbol_name not in symbols:
            raise ValueError(f"missing table symbol: {symbol_name}")
        base = symbols[symbol_name]
        entries = []
        for index in range(count):
            address = base + index * 4
            rom_data = rom.read_bytes()
            if address + 4 > len(rom_data):
                raise ValueError(f"table {name} extends beyond ROM at {_hex(address)}")
            target = struct.unpack_from(">I", rom_data, address)[0] & 0xFFFFFF
            valid = 0 < target < len(rom_data) and target % 2 == 0
            if valid:
                valid_entry_count += 1
            else:
                invalid_entry_count += 1
            scenarios = sorted(observed[name].get(target, set())) if valid else []
            covered = valid and bool(scenarios)
            if covered:
                covered_entry_count += 1
            elif valid:
                uncovered_entry_count += 1
            entries.append({
                "index": index,
                "address": _hex(address),
                "target": _hex(target),
                "valid": valid,
                "covered": covered,
                "scenarios": scenarios,
            })
        valid_entries = [entry for entry in entries if entry["valid"]]
        covered_entries = [entry for entry in valid_entries if entry["covered"]]
        tables.append({
            "name": name,
            "symbol": symbol_name,
            "address": _hex(base),
            "entry_count": count,
            "valid_entry_count": len(valid_entries),
            "covered_entry_count": len(covered_entries),
            "uncovered_entry_count": len(valid_entries) - len(covered_entries),
            "unique_target_count": len({entry["target"] for entry in valid_entries}),
            "covered_unique_target_count": len({entry["target"] for entry in covered_entries}),
            "entries": entries,
        })

    return {
        "format": FORMAT,
        "coverage_format": coverage["format"],
        "rom_sha256": actual_hashes["sha256"],
        "tables": tables,
        "summary": {
            "table_count": len(tables),
            "valid_entry_count": valid_entry_count,
            "covered_entry_count": covered_entry_count,
            "uncovered_entry_count": uncovered_entry_count,
            "invalid_entry_count": invalid_entry_count,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "coverage",
        nargs="?",
        type=Path,
        default=ROOT / "build/re/coverage.json",
    )
    parser.add_argument("--rom", type=Path, default=ROOT / "rom/Disneys_Aladdin_U_p1.bin")
    parser.add_argument("--output", type=Path, default=ROOT / "build/re/coverage-gaps.json")
    args = parser.parse_args()
    try:
        coverage = _load_json(args.coverage.resolve())
        report = build_gap_report(coverage, args.rom.resolve())
        write_json(args.output.resolve(), report)
    except (OSError, ValueError, struct.error) as error:
        raise SystemExit(str(error)) from error

    for table in report["tables"]:
        print(
            f"{table['name']:18} "
            f"{table['covered_entry_count']:3}/{table['valid_entry_count']:3} covered, "
            f"{table['uncovered_entry_count']:3} gaps"
        )
    print(f"report: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
