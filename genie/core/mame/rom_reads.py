"""Summarize ROM-read tap records with canonical consumer context."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Iterable

from genie.common import ROOT, parse_int, write_json
from genie.ghidra.database import AnalysisDatabase
from genie.symbols import SymbolStore

try:
    from .coverage import discover_trace_dirs
except ImportError:  # direct execution through Genie run_tool
    from genie.core.mame.coverage import discover_trace_dirs


FORMAT = "openaladdin-rom-read-coverage-v1"


def _address(value: Any) -> int:
    return parse_int(value)


def _hex(value: int) -> str:
    return f"0x{value:08X}"


def _trace_path(path: Path) -> Path:
    return path / "trace_boot.jsonl" if path.is_dir() else path


def _scenario_name(trace_path: Path, trace_root: Path | None) -> str:
    directory = trace_path.parent.resolve()
    if trace_root is not None:
        try:
            relative = directory.relative_to(trace_root.resolve())
            if str(relative) != ".":
                return relative.as_posix()
        except ValueError:
            pass
    return directory.name


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    records = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"{path}:{line_number}: invalid JSON: {error}") from error
        if not isinstance(value, dict):
            raise ValueError(f"{path}:{line_number}: trace record is not an object")
        records.append(value)
    return records


def summarize_rom_reads(
    trace_dirs: Iterable[Path],
    *,
    database: AnalysisDatabase,
    symbols: SymbolStore,
    output: Path | None = None,
    trace_root: Path | None = None,
) -> dict[str, Any]:
    """Map ROM-read taps to symbols and the functions that caused them."""

    targets: dict[tuple[int, int], dict[str, Any]] = {}
    scenarios: list[dict[str, Any]] = []
    rom_hashes: set[str] = set()
    paths = [_trace_path(Path(path).resolve()) for path in trace_dirs]
    if not paths:
        raise ValueError("no MAME traces found")

    for trace_path in paths:
        if not trace_path.is_file():
            raise ValueError(f"trace not found: {trace_path}")
        records = _read_jsonl(trace_path)
        header = next((record for record in records if record.get("type") == "header"), None)
        if header is None:
            raise ValueError(f"trace has no header: {trace_path.parent}")
        scenario = _scenario_name(trace_path, trace_root)
        rom_sha256 = str(header.get("rom_sha256") or "")
        if rom_sha256:
            rom_hashes.add(rom_sha256)
        read_records = [record for record in records if record.get("type") == "rom_read"]
        scenarios.append({
            "name": scenario,
            "trace_dir": str(trace_path.parent),
            "rom_sha256": rom_sha256,
            "read_count": len(read_records),
        })
        for record in read_records:
            try:
                start = _address(record.get("range_start", record.get("address")))
                end = _address(record.get("range_end", record.get("address")))
                address = _address(record.get("address"))
                pc = _address(record.get("pc", 0))
                frame = int(record.get("frame", 0))
            except (TypeError, ValueError):
                continue
            if end < start:
                continue
            key = (start, end)
            target = targets.setdefault(key, {
                "range_start": _hex(start),
                "range_end": _hex(end),
                "symbol": None,
                "read_count": 0,
                "addresses": set(),
                "scenarios": set(),
                "consumers": {},
                "first_frame": frame,
                "last_frame": frame,
            })
            if target["symbol"] is None:
                symbol = symbols.at(start, include_ranges=True)
                if symbol is not None:
                    target["symbol"] = {
                        "address": _hex(symbol.address),
                        "end": _hex(symbol.end) if symbol.end is not None else None,
                        "kind": symbol.kind,
                        "name": symbol.name,
                    }
            target["read_count"] += 1
            target["addresses"].add(address)
            target["scenarios"].add(scenario)
            target["first_frame"] = min(target["first_frame"], frame)
            target["last_frame"] = max(target["last_frame"], frame)

            function = database.function(pc) if pc else None
            function_address = _address(function["address"]) if function else None
            consumer_key = (pc, function_address)
            consumer = target["consumers"].setdefault(consumer_key, {
                "pc": _hex(pc),
                "function": _hex(function_address) if function_address is not None else None,
                "function_name": function.get("name") if function else None,
                "read_count": 0,
                "addresses": set(),
                "scenarios": set(),
                "first_frame": frame,
                "last_frame": frame,
            })
            consumer["read_count"] += 1
            consumer["addresses"].add(address)
            consumer["scenarios"].add(scenario)
            consumer["first_frame"] = min(consumer["first_frame"], frame)
            consumer["last_frame"] = max(consumer["last_frame"], frame)

    if len(rom_hashes) > 1:
        raise ValueError(f"ROM read traces use different ROMs: {sorted(rom_hashes)}")

    normalized_targets = []
    for _, target in sorted(targets.items()):
        normalized_targets.append({
            "range_start": target["range_start"],
            "range_end": target["range_end"],
            "symbol": target["symbol"],
            "read_count": target["read_count"],
            "addresses": [_hex(address) for address in sorted(target["addresses"])],
            "scenarios": sorted(target["scenarios"]),
            "first_frame": target["first_frame"],
            "last_frame": target["last_frame"],
            "consumers": [
                {
                    "pc": consumer["pc"],
                    "function": consumer["function"],
                    "function_name": consumer["function_name"],
                    "read_count": consumer["read_count"],
                    "addresses": [_hex(address) for address in sorted(consumer["addresses"])],
                    "scenarios": sorted(consumer["scenarios"]),
                    "first_frame": consumer["first_frame"],
                    "last_frame": consumer["last_frame"],
                }
                for _, consumer in sorted(target["consumers"].items())
            ],
        })
    report = {
        "format": FORMAT,
        "rom_sha256": next(iter(rom_hashes), ""),
        "scenarios": sorted(scenarios, key=lambda item: item["name"]),
        "targets": normalized_targets,
        "summary": {
            "scenario_count": len(scenarios),
            "target_count": len(normalized_targets),
            "read_count": sum(target["read_count"] for target in normalized_targets),
            "consumer_count": sum(len(target["consumers"]) for target in normalized_targets),
        },
    }
    if output is not None:
        write_json(output, report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace_dirs", nargs="*", type=Path)
    parser.add_argument("--trace-root", type=Path, default=ROOT / "build/re/traces")
    parser.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    parser.add_argument("--output", type=Path, default=ROOT / "build/re/rom-reads.json")
    parser.add_argument("--json", action="store_true", dest="json_output")
    args = parser.parse_args()
    trace_root = args.trace_root.resolve()
    paths = (
        [path.resolve() for path in args.trace_dirs]
        if args.trace_dirs
        else discover_trace_dirs(trace_root)
    )
    try:
        report = summarize_rom_reads(
            paths,
            database=AnalysisDatabase(args.database.resolve()),
            symbols=SymbolStore(root=ROOT),
            output=args.output.resolve(),
            trace_root=trace_root,
        )
    except (OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(str(error)) from error
    if args.json_output:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        summary = report["summary"]
        print(
            f"ROM reads: {summary['scenario_count']} scenario(s), "
            f"{summary['read_count']} read(s), "
            f"{summary['target_count']} target range(s), "
            f"{summary['consumer_count']} consumer(s)"
        )
        print(f"report: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
