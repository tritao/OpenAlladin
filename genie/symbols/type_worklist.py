"""Evidence-ranked inventory for numeric actor and event identities."""

from __future__ import annotations

from collections import Counter
import json
from pathlib import Path
import re
from typing import Any, Iterable

from genie.common import parse_int
from genie.ghidra.database import AnalysisDatabase

from .entities import SemanticMapping, mappings_by_symbol
from .model import Symbol
from .store import SymbolStore


# The first expression handles the normal ``TYPE84`` and ``RuntimeType20``
# spellings.  The second handles CamelCase names such as ``Type84Base`` where
# the first letter of the following word is also a hexadecimal digit.
_TYPE_RE = re.compile(
    r"(?i)(?<![A-Z0-9])(?:RUNTIME_?|ACTOR_?|PLAYER_?|INTERACTION_?)?TYPE_?([0-9A-F]{1,4})"
)
_CAMEL_TYPE_RE = re.compile(
    r"(?i)(?<![A-Z0-9])(?:RUNTIME_?|ACTOR_?|PLAYER_?|INTERACTION_?)?TYPE_?([0-9]{2})(?=[A-Z][a-z])"
)
_HEX_TOKEN_RE = re.compile(r"(?i)^([0-9A-F]{1,4})$")

_GENERIC_TOKENS = frozenset({
    "ACTOR", "ANIM", "ANIMATION", "CREATE", "HANDLER", "MOVE", "MOVEMENT",
    "PLAYER", "TEMPLATE", "TABLE", "TYPE", "VM", "WITH", "FROM", "FOR", "AND", "OR",
})
_EVENT_HINTS = frozenset({
    "ACTION", "ATTACK", "COLLISION", "DEATH", "EVENT", "EXIT", "GATE", "INTERACTION",
    "LANDING", "LEVEL", "PRESENTATION", "PROXIMITY", "RESPONSE", "SCENE", "SPAWN", "TERRAIN", "TRANSITION",
    "WALL", "RECOVERY", "BOUNCE", "HIT", "LATCH", "COOLDOWN",
})
_ROLE_HINTS = frozenset({
    "AUXILIARY", "BASE", "COUNT", "CURSOR", "DATA", "DELAY", "DIGITS", "FLAG", "LOOP",
    "MARKER", "OFFSET", "PAIR", "PREFIX", "SOURCE", "STEP", "STREAM", "VARIANT",
})


def _address(value: Any) -> int:
    return parse_int(value)


def _type_match_ids(name: str, match: re.Match[str]) -> list[int]:
    values = [int(match.group(1), 16)]
    end = match.end()
    # ``Type84Base_B6`` uses a CamelCase role between the selector and an
    # additional numeric variant.  Skip that role before looking for the
    # associated underscore-delimited selector.
    while end < len(name) and name[end].isalpha():
        end += 1
    while end < len(name) and name[end] == "_":
        suffix = re.match(r"_([0-9A-Fa-f]{1,4})(?![A-Za-z0-9])", name[end:])
        if suffix is None:
            break
        values.append(int(suffix.group(1), 16))
        end += suffix.end()
    return values


def numeric_type_ids(name: str) -> tuple[int, ...]:
    """Return technical numeric selectors encoded in a canonical name."""

    values: list[int] = []
    spans: list[tuple[int, int]] = []
    for expression in (_CAMEL_TYPE_RE, _TYPE_RE):
        for match in expression.finditer(str(name)):
            if any(start <= match.start() < end for start, end in spans):
                continue
            spans.append(match.span())
            values.extend(_type_match_ids(str(name), match))
    return tuple(dict.fromkeys(values))


def _meaningful_tokens(name: str) -> tuple[str, ...]:
    tokens = [token for token in re.split(r"_+", str(name).upper()) if token]
    result: list[str] = []
    for token in tokens:
        if _HEX_TOKEN_RE.fullmatch(token) or token in _GENERIC_TOKENS:
            continue
        # Remove the numeric portions from a token such as TYPE84BASE.  The
        # CamelCase spelling is still retained as a useful candidate hint.
        token = re.sub(r"(?i)^TYPE[0-9A-F]{1,4}", "", token)
        if token and not _HEX_TOKEN_RE.fullmatch(token):
            result.append(token)
    return tuple(result)


def candidate_class(name: str, *, mapped: bool = False) -> str:
    """Classify the naming task without claiming an entity identity."""

    if mapped:
        return "mapped"
    tokens = set(_meaningful_tokens(name))
    entity_tokens = tokens - _EVENT_HINTS - _ROLE_HINTS
    if entity_tokens and ("ACTOR" in str(name).upper() or "PLAYER" in str(name).upper()):
        return "entity"
    if tokens & _EVENT_HINTS:
        return "event"
    if tokens:
        return "role"
    return "technical_only"


def _scope(name: str) -> str:
    upper = str(name).upper()
    if "INTERACTION" in upper:
        return "event"
    if "TERRAIN" in upper or "LEVEL" in upper or "SCENE" in upper:
        return "event"
    if "ACTOR" in upper or "PLAYER" in upper or "ANIM" in upper or "MOVE" in upper:
        return "actor"
    if "RESOURCE" in upper or "GRAPHICS" in upper or "AUDIO" in upper:
        return "resource"
    return "technical"


def _records(database: AnalysisDatabase | None, filename: str) -> list[dict[str, Any]]:
    if database is None:
        return []
    try:
        document = database.load(filename)
    except (FileNotFoundError, json.JSONDecodeError):
        return []
    if isinstance(document, dict):
        values = document.get("references", document.get("tables", document.get("functions", [])))
    else:
        values = document
    return [dict(item) for item in values if isinstance(item, dict)] if isinstance(values, list) else []


def _in_symbol_range(value: Any, symbol: Symbol) -> bool:
    try:
        address = _address(value)
    except (TypeError, ValueError):
        return False
    symbol_range = symbol.range or (symbol.address, symbol.address)
    return symbol_range[0] <= address <= symbol_range[1]


def _runtime_functions(database: AnalysisDatabase | None, coverage_path: Path | None) -> dict[int, dict[str, Any]]:
    if coverage_path is None and database is not None:
        for name in ("coverage-expanded.json", "coverage-ghidra.json", "coverage.json"):
            candidate = database.root.parent / name
            if candidate.is_file():
                coverage_path = candidate
                break
    if coverage_path is None or not coverage_path.is_file():
        return {}
    try:
        document = json.loads(coverage_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    functions = document.get("functions") if isinstance(document, dict) else None
    if isinstance(functions, dict):
        return {
            _address(address): {
                "pc_count": int(value.get("pc_count", value.get("sample_count", 0)) or 0),
                "scenarios": sorted(str(item) for item in value.get("scenarios", ()) or ()),
            }
            for address, value in functions.items()
            if isinstance(value, dict)
        }
    return {}


def _function_evidence(
    database: AnalysisDatabase | None,
    symbols: SymbolStore,
    records: Iterable[dict[str, Any]],
    runtime: dict[int, dict[str, Any]],
) -> list[dict[str, Any]]:
    addresses: set[int] = set()
    if database is None:
        return []
    for record in records:
        value = record.get("from_function", record.get("from"))
        if value is None:
            continue
        try:
            address = _address(value)
        except (TypeError, ValueError):
            continue
        if record.get("from_function") is None:
            function = database.function(address)
            if function is None:
                continue
            address = _address(function["address"])
        addresses.add(address)
    result: list[dict[str, Any]] = []
    for address in sorted(addresses):
        function = database.function(address)
        canonical = symbols.at(address, include_ranges=False)
        item = runtime.get(address, {})
        result.append({
            "address": f"0x{address:08X}",
            "name": canonical.name if canonical is not None else str(function.get("name", f"Func_{address:08X}")) if function else f"Func_{address:08X}",
            "runtime_observed": bool(item),
            "runtime_pc_count": int(item.get("pc_count", 0) or 0),
            "runtime_scenarios": list(item.get("scenarios", ())),
        })
    return result


def _jump_table_targets(database: AnalysisDatabase | None) -> set[int]:
    targets: set[int] = set()
    for table in _records(database, "jump_tables.json"):
        for key in ("targets", "destinations", "addresses", "entries"):
            values = table.get(key)
            if not isinstance(values, list):
                continue
            for value in values:
                raw = value.get("target") if isinstance(value, dict) else value
                try:
                    targets.add(_address(raw))
                except (TypeError, ValueError):
                    continue
    return targets


def numeric_type_inventory(symbols: Iterable[Symbol]) -> dict[str, Any]:
    """Return aggregate progress metrics for numeric canonical names."""

    values = [symbol for symbol in symbols if numeric_type_ids(symbol.name)]
    by_kind = Counter(symbol.kind for symbol in values)
    by_class = Counter(candidate_class(symbol.name) for symbol in values)
    return {
        "numeric_canonical_names": len(values),
        "by_kind": dict(sorted(by_kind.items())),
        "by_candidate_class": dict(sorted(by_class.items())),
        "unique_technical_type_ids": len({value for symbol in values for value in numeric_type_ids(symbol.name)}),
        "semantic_candidates": sum(by_class[key] for key in ("entity", "event", "role")),
        "technical_only": by_class["technical_only"],
    }


def numeric_type_work_queue(
    database: AnalysisDatabase | None,
    symbols: SymbolStore,
    *,
    mappings: Iterable[SemanticMapping] = (),
    coverage_path: Path | None = None,
    kind: str | None = None,
) -> list[dict[str, Any]]:
    """Rank numeric names by evidence and semantic-renaming opportunity."""

    normalized_kind = str(kind).casefold() if kind else None
    if normalized_kind is not None and normalized_kind not in {"function", "ram", "data"}:
        raise ValueError("numeric type worklist kind must be function, ram, data, or omitted")
    mappings = tuple(mappings)
    by_symbol = mappings_by_symbol(mappings)
    by_type: dict[int, list[SemanticMapping]] = {}
    for mapping in mappings:
        for value in mapping.technical_types:
            by_type.setdefault(value, []).append(mapping)

    runtime = _runtime_functions(database, coverage_path)
    xrefs = _records(database, "xrefs.json")
    reads = _records(database, "memory_reads.json")
    writes = _records(database, "memory_writes.json")
    jump_targets = _jump_table_targets(database)
    result: list[dict[str, Any]] = []

    for symbol in symbols.symbols:
        if normalized_kind and symbol.kind.casefold() != normalized_kind:
            continue
        type_ids = numeric_type_ids(symbol.name)
        if not type_ids:
            continue
        mapping = by_symbol.get(symbol.address)
        # A type-level mapping is useful only when the selector has one
        # curated meaning. Shared temporary/response types such as 0x84 are
        # intentionally allowed to map to several roles; those require an
        # address-specific mapping and must not be auto-promoted here.
        type_mappings = [
            item
            for value in type_ids
            if len(by_type.get(value, ())) == 1
            for item in by_type.get(value, ())
        ]
        mapping_matches = []
        seen_mapping_names: set[str] = set()
        for item, match_kind in [(mapping, "symbol") for mapping in (mapping,)] + [
            (item, "technical_type") for item in type_mappings
        ]:
            if item is not None and item.name.casefold() not in seen_mapping_names:
                seen_mapping_names.add(item.name.casefold())
                mapping_matches.append({"name": item.name, "scope": item.scope, "match": match_kind})

        incoming_xrefs = [item for item in xrefs if _in_symbol_range(item.get("to"), symbol)]
        symbol_reads = [item for item in reads if _in_symbol_range(item.get("to"), symbol)]
        symbol_writes = [item for item in writes if _in_symbol_range(item.get("to"), symbol)]
        evidence_records = incoming_xrefs + symbol_reads + symbol_writes
        functions = _function_evidence(database, symbols, evidence_records, runtime)
        runtime_functions = [item for item in functions if item["runtime_observed"]]
        references = {
            address for item in functions
            for address in [item["address"]]
        }
        is_jump_target = symbol.address in jump_targets
        category = candidate_class(symbol.name, mapped=bool(mapping_matches))
        score = (
            (100_000 if runtime_functions else 0)
            + len(runtime_functions) * 10_000
            + len(symbol_writes) * 5_000
            + len(symbol_reads) * 2_500
            + len(incoming_xrefs) * 100
            + (2_000 if is_jump_target else 0)
            + (500 if category in {"entity", "event", "role"} else 0)
        )
        result.append({
            "rank": 0,
            "score": score,
            "address": f"0x{symbol.address:08X}",
            "name": symbol.name,
            "kind": symbol.kind,
            "confidence": symbol.confidence,
            "range": {"start": symbol.range[0], "end": symbol.range[1]} if symbol.range else None,
            "technical_type_ids": [f"0x{value:02X}" if value <= 0xFF else f"0x{value:04X}" for value in type_ids],
            "scope": _scope(symbol.name),
            "candidate_class": category,
            "mapping_status": "mapped" if mapping_matches else "needs_mapping" if category != "technical_only" else "technical_only",
            "mapping_matches": mapping_matches,
            "description": symbol.description or "",
            "aliases": list(symbol.aliases),
            "xrefs": len(incoming_xrefs),
            "reads": len(symbol_reads),
            "writes": len(symbol_writes),
            "evidence_function_count": len(references),
            "evidence_functions": functions,
            "runtime_observed": bool(runtime_functions),
            "runtime_function_count": len(runtime_functions),
            "runtime_scenarios": sorted({scenario for item in runtime_functions for scenario in item["runtime_scenarios"]}),
            "jump_table_target": is_jump_target,
            "action": (
                "use curated semantic mapping"
                if mapping_matches else "investigate and map to an entity/event/role"
                if category != "technical_only" else "retain technical type until evidence identifies a meaning"
            ),
        })
    result.sort(key=lambda item: (-item["score"], item["candidate_class"], _address(item["address"])))
    for rank, item in enumerate(result, 1):
        item["rank"] = rank
    return result


def render_type_worklist(
    items: list[dict[str, Any]],
    *,
    total: int,
    inventory: dict[str, Any],
    json_output: bool,
    database_available: bool,
) -> None:
    if json_output:
        print(json.dumps({
            "total": total,
            "inventory": inventory,
            "database_available": database_available,
            "items": items,
        }, indent=2, sort_keys=True))
        return
    print(f"Numeric entity/event worklist ({len(items)} of {total})")
    print(
        f"canonical={inventory['numeric_canonical_names']} "
        f"semantic_candidates={inventory['semantic_candidates']} "
        f"technical_only={inventory['technical_only']} "
        f"unique_types={inventory['unique_technical_type_ids']} "
        f"database={'yes' if database_available else 'no'}"
    )
    if not items:
        return
    print("rank score address       kind     class          types           name")
    for item in items:
        print(
            f"{item['rank']:>4} {item['score']:>5} {item['address']} "
            f"{item['kind']:<8} {item['candidate_class']:<14} "
            f"{','.join(item['technical_type_ids']):<15} {item['name'][:48]}"
        )
        print(
            f"     evidence: xrefs={item['xrefs']},reads={item['reads']},writes={item['writes']},"
            f"functions={item['evidence_function_count']},run={'yes' if item['runtime_observed'] else 'no'},"
            f"jump={'yes' if item['jump_table_target'] else 'no'}"
        )
        print(f"     action: {item['action']}")


__all__ = [
    "candidate_class",
    "numeric_type_ids",
    "numeric_type_inventory",
    "numeric_type_work_queue",
    "render_type_worklist",
]
