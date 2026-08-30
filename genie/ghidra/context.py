"""Unified reverse-engineering context for one ROM address."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from genie.common import parse_int
from genie.layout.model import Layout
from genie.symbols import SymbolStore

from .database import AnalysisDatabase


def _address(value: Any) -> int:
    return parse_int(value)


def _symbol_value(
    store: SymbolStore,
    address: int,
    *,
    include_ranges: bool = False,
) -> dict[str, Any] | None:
    symbol = store.at(address, include_ranges=include_ranges)
    return symbol.to_dict() if symbol is not None else None


def _runtime(database: AnalysisDatabase, function_address: int, path: Path | None) -> dict[str, Any] | None:
    if path is None:
        candidate = database.root.parent / "coverage-ghidra.json"
        if not candidate.is_file():
            candidate = database.root.parent / "coverage.json"
        path = candidate
    else:
        path = Path(path)
    if not path.is_file():
        return None
    document = json.loads(path.read_text(encoding="utf-8"))
    functions = document.get("functions") if isinstance(document, dict) else None
    if isinstance(functions, dict):
        for raw_address, raw_value in functions.items():
            try:
                if _address(raw_address) != function_address:
                    continue
            except (TypeError, ValueError):
                continue
            value = dict(raw_value) if isinstance(raw_value, dict) else {}
            return {
                "observed": True,
                "pc_count": int(value.get("pc_count", value.get("sample_count", 0)) or 0),
                "scenarios": sorted(str(item) for item in value.get("scenarios", ()) or ()),
                "source": str(path),
            }
        return {"observed": False, "pc_count": 0, "scenarios": [], "source": str(path)}

    pcs = document.get("pcs", []) if isinstance(document, dict) else []
    if isinstance(pcs, dict):
        pc_items = [
            {"address": raw_address, **(raw_value if isinstance(raw_value, dict) else {})}
            for raw_address, raw_value in pcs.items()
        ]
    else:
        pc_items = pcs if isinstance(pcs, list) else []
    counts = 0
    scenarios: set[str] = set()
    for item in pc_items:
        try:
            function = database.function(_address(item.get("address")))
        except (AttributeError, TypeError, ValueError):
            continue
        if function is not None and _address(function["address"]) == function_address:
            counts += 1
            scenarios.update(str(scenario) for scenario in item.get("scenarios", ()) or ())
    return {"observed": bool(counts), "pc_count": counts, "scenarios": sorted(scenarios), "source": str(path)}


def build_context(
    database: AnalysisDatabase,
    address: int,
    symbols: SymbolStore,
    *,
    layout_path: Path | None = None,
    coverage_path: Path | None = None,
    radius: int = 2,
    include_decompile: bool = False,
) -> dict[str, Any]:
    address = _address(address)
    function = database.function(address)
    function_address = _address(function["address"]) if function else address
    function_symbol = _symbol_value(symbols, function_address) if function else None
    layout_value: Layout | None = None
    if layout_path is None:
        layout_path = database.root / "layout.json"
    else:
        layout_path = Path(layout_path)
    if layout_path.is_file():
        layout_value = Layout.from_dict(json.loads(layout_path.read_text(encoding="utf-8")))

    layout_range = layout_value.at(address) if layout_value else None
    nearby: list[dict[str, Any]] = []
    if layout_value and layout_range is not None:
        index = layout_value.ranges.index(layout_range)
        start = max(0, index - max(0, radius))
        stop = min(len(layout_value.ranges), index + max(0, radius) + 1)
        nearby = [item.to_dict() for item in layout_value.ranges[start:stop]]

    callers = database.callers(function_address) if function else []
    callees = database.callees(function_address) if function else []
    incoming_xrefs = database.xrefs(address)
    outgoing_xrefs = database.function_references(function_address, "xrefs.json") if function else []
    reads = database.function_references(function_address, "memory_reads.json") if function else database.readers(address)
    writes = database.function_references(function_address, "memory_writes.json") if function else database.writers(address)
    referenced_addresses = {
        _address(item["to"])
        for item in (*callees, *outgoing_xrefs)
        if item.get("to") is not None
    }
    known_symbols = [
        symbol.to_dict()
        for target in sorted(referenced_addresses)
        for symbol in [symbols.at(target, include_ranges=False)]
        if symbol is not None
    ]
    decompile_path = database.root / "decompile" / f"{function_address:08X}.txt"
    decompile: dict[str, Any] | None = None
    if function:
        decompile = {
            "available": decompile_path.is_file(),
            "path": str(decompile_path),
        }
        if include_decompile and decompile_path.is_file():
            decompile["text"] = decompile_path.read_text(encoding="utf-8")
    return {
        "address": f"0x{address:08X}",
        "symbol": _symbol_value(symbols, address, include_ranges=True),
        "function_symbol": function_symbol,
        "function": function,
        "callers": callers,
        "callees": callees,
        "xrefs": incoming_xrefs,
        "outgoing_xrefs": outgoing_xrefs,
        "ram_reads": reads,
        "ram_writes": writes,
        "known_symbols_referenced": known_symbols,
        "layout": layout_range.to_dict() if layout_range else None,
        "nearby_layout": nearby,
        "runtime": _runtime(database, function_address, coverage_path) if function else None,
        "decompile": decompile,
    }


__all__ = ["build_context"]
