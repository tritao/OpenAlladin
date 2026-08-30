"""Evidence-ranked research queues built from the offline analysis database."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from genie.common import parse_int
from genie.symbols import SymbolStore

from .database import AnalysisDatabase


LOW_CONFIDENCE = frozenset({"unknown", "provisional", "probable"})


def _address(value: Any) -> int:
    return parse_int(value)


def _runtime_functions(database: AnalysisDatabase, path: Path | None) -> dict[int, dict[str, Any]]:
    if path is None:
        candidate = database.root.parent / "coverage-ghidra.json"
        if not candidate.is_file():
            candidate = database.root.parent / "coverage.json"
        path = candidate
    if not path.is_file():
        return {}
    document = json.loads(path.read_text(encoding="utf-8"))
    result: dict[int, dict[str, Any]] = {}
    functions = document.get("functions") if isinstance(document, dict) else None
    if isinstance(functions, dict):
        for raw_address, raw_value in functions.items():
            try:
                address = _address(raw_address)
            except (TypeError, ValueError):
                continue
            value = dict(raw_value) if isinstance(raw_value, dict) else {}
            result[address] = {
                "pc_count": int(value.get("pc_count", value.get("sample_count", 0)) or 0),
                "scenarios": sorted(str(item) for item in value.get("scenarios", ()) or ()),
                "source": str(path),
            }
        return result

    pcs = document.get("pcs", []) if isinstance(document, dict) else []
    for raw_pc in pcs if isinstance(pcs, list) else ():
        try:
            function = database.function(_address(raw_pc.get("address")))
        except (AttributeError, TypeError, ValueError):
            continue
        if function is None:
            continue
        address = _address(function["address"])
        item = result.setdefault(address, {"pc_count": 0, "scenarios": [], "source": str(path)})
        item["pc_count"] += 1
        item["scenarios"] = sorted(set(item["scenarios"]) | {
            str(scenario) for scenario in raw_pc.get("scenarios", ()) or ()
        })
    return result


def _jump_table_membership(database: AnalysisDatabase) -> dict[int, list[str]]:
    document = database.load("jump_tables.json")
    tables = document.get("tables", []) if isinstance(document, dict) else document
    result: dict[int, list[str]] = {}
    for index, table in enumerate(tables if isinstance(tables, list) else ()):
        if not isinstance(table, dict):
            continue
        label = str(table.get("name") or table.get("address") or f"table_{index}")
        values: list[Any] = []
        for key in ("targets", "destinations", "addresses", "entries"):
            candidate = table.get(key)
            if isinstance(candidate, list):
                values.extend(candidate)
        for value in values:
            raw_target = value.get("target") if isinstance(value, dict) else value
            try:
                target = _address(raw_target)
            except (TypeError, ValueError):
                continue
            result.setdefault(target, []).append(label)
    return {address: sorted(set(labels)) for address, labels in result.items()}


def function_work_queue(
    database: AnalysisDatabase,
    symbols: SymbolStore,
    *,
    coverage_path: Path | None = None,
) -> list[dict[str, Any]]:
    """Rank mechanically named functions by actionable static/runtime evidence."""

    runtime = _runtime_functions(database, coverage_path)
    dispatch = _jump_table_membership(database)
    result: list[dict[str, Any]] = []
    for function in database.functions:
        address = _address(function["address"])
        canonical = symbols.at(address, include_ranges=False)
        if (
            canonical is not None
            and canonical.kind == "function"
            and not canonical.is_mechanical
            and canonical.confidence not in LOW_CONFIDENCE
        ):
            continue
        callers = database.callers(address)
        callees = database.callees(address)
        reads = database.function_references(address, "memory_reads.json")
        writes = database.function_references(address, "memory_writes.json")
        ram_reads = sorted({_address(item["to"]) for item in reads if 0xFF0000 <= _address(item["to"]) <= 0xFFFFFF})
        ram_writes = sorted({_address(item["to"]) for item in writes if 0xFF0000 <= _address(item["to"]) <= 0xFFFFFF})
        observed = runtime.get(address, {})
        dispatch_tables = dispatch.get(address, [])
        neighbor_addresses = {
            _address(item["from"])
            for item in callers
            if item.get("from") is not None
        } | {
            _address(item["to"])
            for item in callees
            if item.get("to") is not None
        }
        known_neighbors = sum(
            symbols.at(target, include_ranges=False) is not None
            for target in neighbor_addresses
        )
        # Runtime execution and dispatch reachability are strong prioritizers;
        # graph, known neighbors, and RAM evidence then provide stable
        # tie-breakers.
        score = (
            (100_000 if observed else 0)
            + len(dispatch_tables) * 5_000
            + len(callers) * 1_000
            + known_neighbors * 100
            + len(ram_writes) * 20
            + len(ram_reads) * 10
            + len(callees)
        )
        result.append({
            "address": f"0x{address:08X}",
            "name": str(function.get("name") or f"Func_{address:08X}"),
            "canonical_name": canonical.name if canonical is not None else None,
            "confidence": canonical.confidence if canonical is not None else "unknown",
            "score": score,
            "callers": len(callers),
            "callees": len(callees),
            "ram_reads": len(ram_reads),
            "ram_writes": len(ram_writes),
            "known_neighbors": known_neighbors,
            "runtime_observed": bool(observed),
            "runtime_pc_count": int(observed.get("pc_count", 0) or 0),
            "runtime_scenarios": list(observed.get("scenarios", ())),
            "dispatch_tables": dispatch_tables,
            "thunk": bool(function.get("thunk", False)),
        })
    result.sort(key=lambda item: (
        -item["score"],
        -int(item["runtime_observed"]),
        -item["callers"],
        -item["ram_writes"],
        _address(item["address"]),
    ))
    for rank, item in enumerate(result, 1):
        item["rank"] = rank
    return result


def render_work_queue(
    items: list[dict[str, Any]],
    *,
    total: int,
    json_output: bool,
    title: str,
) -> None:
    if json_output:
        print(json.dumps({"total": total, "items": items}, indent=2, sort_keys=True))
        return
    print(f"{title} ({len(items)} of {total})")
    if not items:
        return
    print("rank score address       name                           callers callees RAM-r RAM-w known run dispatch")
    for item in items:
        print(
            f"{item['rank']:>4} {item['score']:>5} {item['address']} "
            f"{item['name'][:30]:<30} {item['callers']:>7} {item['callees']:>7} "
            f"{item['ram_reads']:>5} {item['ram_writes']:>5} {item['known_neighbors']:>5} "
            f"{'yes' if item['runtime_observed'] else 'no':>3} {len(item['dispatch_tables']):>8}"
        )


__all__ = ["function_work_queue", "render_work_queue"]
