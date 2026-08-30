"""Aggregate semantic reverse-engineering coverage into one stable report."""

from __future__ import annotations

from collections import Counter
import json
from pathlib import Path
from typing import Any

from genie.common import ROOT, parse_int
from genie.data import DataIndex, STREAM_KINDS
from genie.ghidra.database import AnalysisDatabase
from genie.profiles import load_profile
from genie.symbols import Symbol, SymbolStore
from genie.symbols.naming import is_mechanical_name


FORMAT = "openaladdin-semantic-coverage-v1"


def _address(value: Any) -> int:
    return parse_int(value)


def _hex(value: int) -> str:
    return f"0x{value:08X}"


def _default_coverage(database: AnalysisDatabase) -> Path:
    parent = database.root.parent
    for name in ("coverage-expanded.json", "coverage-ghidra.json", "coverage.json"):
        candidate = parent / name
        if candidate.is_file():
            return candidate
    return parent / "coverage.json"


def _load_json(path: Path) -> Any | None:
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def _runtime_functions(database: AnalysisDatabase, path: Path) -> dict[int, dict[str, Any]]:
    document = _load_json(path)
    if not isinstance(document, dict):
        return {}
    result: dict[int, dict[str, Any]] = {}
    functions = document.get("functions")
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
            }
        return result

    pcs = document.get("pcs", [])
    if isinstance(pcs, dict):
        pc_items = [
            {"address": raw_address, **(raw_value if isinstance(raw_value, dict) else {})}
            for raw_address, raw_value in pcs.items()
        ]
    else:
        pc_items = pcs if isinstance(pcs, list) else []
    for item in pc_items:
        if not isinstance(item, dict):
            continue
        try:
            function = database.function(_address(item.get("address")))
        except (AttributeError, TypeError, ValueError):
            continue
        if function is None:
            continue
        address = _address(function["address"])
        value = result.setdefault(address, {"pc_count": 0, "scenarios": []})
        value["pc_count"] += 1
        value["scenarios"] = sorted(set(value["scenarios"]) | {
            str(scenario) for scenario in item.get("scenarios", ()) or ()
        })
    return result


def _function_coverage(
    database: AnalysisDatabase,
    symbols: SymbolStore,
    runtime_path: Path,
) -> dict[str, Any]:
    functions = database.functions
    runtime = _runtime_functions(database, runtime_path)
    confidence = Counter()
    semantic = 0
    canonical = 0
    decompiled = 0
    runtime_observed = 0
    runtime_pc_count = 0
    for function in functions:
        address = _address(function["address"])
        symbol = symbols.at(address, include_ranges=False)
        if symbol is not None and symbol.kind == "function":
            canonical += 1
            confidence[symbol.confidence] += 1
            if not is_mechanical_name(symbol.name):
                semantic += 1
        if (database.root / "decompile" / f"{address:08X}.txt").is_file():
            decompiled += 1
        observed = runtime.get(address)
        if observed:
            runtime_observed += 1
            runtime_pc_count += int(observed.get("pc_count", 0) or 0)
    return {
        "discovered": len(functions),
        "canonical": canonical,
        "semantic": semantic,
        "mechanical": len(functions) - semantic,
        "decompiled": decompiled,
        "runtime_observed": runtime_observed,
        "runtime_pc_count": runtime_pc_count,
        "confidence": dict(sorted(confidence.items())),
    }


def _object_coverage(index: DataIndex) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    values = index.objects()
    contexts: list[dict[str, Any]] = []
    by_kind: dict[str, list[tuple[dict[str, Any], dict[str, Any]]]] = {}
    for value in values:
        context = index.context(_address(value["start"]))
        if context is None:
            continue
        contexts.append(context)
        by_kind.setdefault(value["kind"], []).append((value, context))

    result: dict[str, Any] = {"total": len(values), "by_kind": {}}
    for kind in sorted(by_kind):
        entries = by_kind[kind]
        canonical = sum(bool(value.get("canonical_symbol")) for value, _ in entries)
        bounded = sum(bool(value["range_bounded"]) for value, _ in entries)
        typed = sum(bool((value.get("canonical_symbol") or {}).get("type")) for value, _ in entries)
        decoded = sum(bool(context.get("decoded")) for _, context in entries)
        consumers = sum(bool(context.get("consumers")) for _, context in entries)
        runtime = sum(bool(context.get("runtime", {}).get("observed")) for _, context in entries)
        result["by_kind"][kind] = {
            "total": len(entries),
            "discovered": len(entries),
            "canonical": canonical,
            "known": canonical,
            "bounded": bounded,
            "unbounded": len(entries) - bounded,
            "typed": typed,
            "decoded": decoded,
            "with_consumers": consumers,
            "without_consumers": len(entries) - consumers,
            "runtime_observed": runtime,
        }
    return result, contexts


def _evidence_tags(symbols: SymbolStore, contexts: list[dict[str, Any]]) -> dict[str, int]:
    values: list[Symbol] = [symbol for symbol in symbols.symbols if symbol.kind != "ram"]
    values.extend(
        Symbol(
            address=_address(context["object"]["start"]),
            name=str(context["object"]["name"]),
            kind="data",
            provenance=tuple(context["object"].get("evidence", ())),
        )
        for context in contexts
        if context["object"].get("canonical_symbol") is None
    )
    tokens = {
        "decompiled": ("decomp", "decompile"),
        "trace_validated": ("mame", "trace", "runtime"),
        "parity_validated": ("parity", "regression", "native"),
    }
    result = {}
    for label, needles in tokens.items():
        result[label] = sum(
            any(
                needle in provenance.casefold()
                for needle in needles
                for provenance in symbol.provenance
            )
            for symbol in values
        )
    return result


def _object_ref(value: dict[str, Any]) -> dict[str, Any]:
    return {
        "address": value["address"],
        "end": value["end"],
        "kind": value["kind"],
        "name": value["name"],
        "size": value["size"],
    }


def _integrity_report(
    database: AnalysisDatabase,
    index: DataIndex,
    contexts: list[dict[str, Any]],
) -> dict[str, Any]:
    layout = index.layout
    gaps = list(layout.gaps()) if layout is not None else []
    unbounded_streams = []
    bounded_without_consumers = []
    overlaps: dict[tuple[str, str], dict[str, Any]] = {}
    for context in contexts:
        value = context["object"]
        if value["kind"] in STREAM_KINDS:
            if not value["range_bounded"]:
                unbounded_streams.append(_object_ref(value))
            elif not context["consumers"]:
                bounded_without_consumers.append(_object_ref(value))
        current_id = value["id"]
        for other in context["overlap"]:
            other_id = f"{other['kind']}:{other['start']}:{other['end']}"
            pair = tuple(sorted((current_id, other_id)))
            overlaps[pair] = {
                "left": current_id,
                "right": other_id,
                "left_name": value["name"],
                "right_name": other["name"],
            }

    known_stream_roots = [
        item for item in unbounded_streams
        if any(
            context["object"]["address"] == item["address"]
            and context["object"].get("canonical_symbol") is not None
            for context in contexts
        )
    ]

    unknown: dict[int, dict[str, Any]] = {}
    try:
        xrefs = database.load("xrefs.json")
        references = xrefs.get("references", []) if isinstance(xrefs, dict) else xrefs
    except (OSError, TypeError, ValueError, json.JSONDecodeError):
        references = []
    rom_size = None
    if layout is not None:
        rom_size = layout.rom_size
    else:
        try:
            rom_size = _address(database.metadata.get("rom_size"))
        except (TypeError, ValueError):
            rom_size = None
    function_ranges = []
    for function in database.functions:
        try:
            function_ranges.append((
                _address(function.get("start", function["address"])),
                _address(function.get("end", function["address"])),
            ))
        except (KeyError, TypeError, ValueError):
            continue
    for reference in references if isinstance(references, list) else ():
        if not isinstance(reference, dict) or reference.get("call"):
            continue
        # The xref export also contains control-flow and memory-access
        # records.  Only DATA xrefs represent ROM pointer/data targets for
        # this integrity check.
        if str(reference.get("type", "")).upper() != "DATA":
            continue
        try:
            target = _address(reference.get("to"))
        except (TypeError, ValueError):
            continue
        if rom_size is not None and not 0 <= target < rom_size:
            continue
        if any(start <= target <= end for start, end in function_ranges):
            continue
        if index.at(target) is not None:
            continue
        layout_range = layout.at(target) if layout is not None else None
        if layout_range is not None and layout_range.layout_class != "UNKNOWN":
            continue
        item = unknown.setdefault(target, {"address": _hex(target), "references": 0, "consumers": set()})
        item["references"] += 1
        consumer = reference.get("from_function_name") or reference.get("from")
        if consumer is not None:
            item["consumers"].add(str(consumer))
    unknown_targets = []
    for item in unknown.values():
        unknown_targets.append({
            "address": item["address"],
            "references": item["references"],
            "consumers": sorted(item["consumers"]),
        })
    unknown_targets.sort(key=lambda item: _address(item["address"]))

    return {
        "unowned_rom_ranges": {
            "count": len(gaps),
            "bytes": sum(item.size for item in gaps),
            "ranges": [item.to_dict() for item in gaps],
        },
        "overlapping_semantic_objects": {
            "count": len(overlaps),
            "objects": [overlaps[key] for key in sorted(overlaps)],
        },
        "unknown_pointer_targets": {
            "count": len(unknown_targets),
            "targets": unknown_targets,
        },
        "known_stream_roots_without_bounded_extents": sorted(
            known_stream_roots,
            key=lambda item: _address(item["address"]),
        ),
        "bounded_streams_without_consumers": sorted(
            bounded_without_consumers,
            key=lambda item: _address(item["address"]),
        ),
    }


def build_semantic_coverage(
    database: AnalysisDatabase,
    *,
    data_index: DataIndex | None = None,
    symbols: SymbolStore | None = None,
    coverage_path: Path | None = None,
    root: Path = ROOT,
) -> dict[str, Any]:
    """Build a semantic coverage report without modifying workspace state."""

    symbols = symbols or (data_index.symbols if data_index is not None else SymbolStore(root=root))
    runtime_path = Path(coverage_path) if coverage_path else _default_coverage(database)
    if data_index is None:
        data_index = DataIndex(
            database,
            root=root,
            symbols=symbols,
            coverage_path=runtime_path,
            providers=load_profile().semantic_providers(),
        )
    functions = _function_coverage(database, symbols, runtime_path)
    objects, contexts = _object_coverage(data_index)
    data_stats = data_index.stats()
    animation_sources = sorted(set(data_index._decoded_sources["animation"].values()))
    movement_sources = sorted(set(data_index._decoded_sources["movement"].values()))
    return {
        "format": FORMAT,
        "sources": {
            "database": str(database.root),
            "layout": str(data_index.database.root / "layout.json") if data_index.layout is not None else None,
            "coverage": str(runtime_path),
            "animation_decoder": animation_sources or None,
            "movement_decoder": movement_sources or None,
        },
        "functions": functions,
        "rom_objects": objects,
        "ram": data_stats["ram"],
        "confidence": {
            "functions": functions["confidence"],
            "rom_objects": dict(sorted(Counter(
                context["object"]["confidence"]
                for context in contexts
            ).items())),
            "evidence_tags": _evidence_tags(symbols, contexts),
        },
        "integrity": _integrity_report(database, data_index, contexts),
    }


__all__ = ["FORMAT", "build_semantic_coverage"]
