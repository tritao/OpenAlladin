"""Regression checks for the generated whole-ROM analysis database."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from genie.common import ROOT, load_yaml, parse_int, rom_entries
from genie.symbols import SymbolStore

from .database import AnalysisDatabase


EXPECTED_FORMAT = "openaladdin-ghidra-full-rom-v1"
REQUIRED_FILES = (
    "metadata.json",
    "functions.json",
    "callgraph.json",
    "xrefs.json",
    "memory_reads.json",
    "memory_writes.json",
    "indirect_calls.json",
    "jump_tables.json",
    "address_classes.json",
)
FRAME_LOOP = 0x001A8C16
FRAME_PHASE_COUNTER = 0x00FF7E28
FRAME_PHASE_COUNTER_WRITER = 0x001A8C1E

KNOWN_FUNCTIONS = {
    "Game_FrameUpdateLoop": 0x001A8C16,
    "AnimationVM_TickActors": 0x001AC784,
    "MovementVM_TickActors": 0x001ADE36,
    "Actor_PlayerCollisionPass": 0x001ABB40,
    "Terrain_ResolvePlayerCell": 0x001B1E38,
}

# These are ROM pointer/dispatch tables recovered independently of Ghidra's
# switch analysis.  They are intentionally checked through xrefs and symbols,
# since a valid Ghidra export may contain no decompiler jump-table records.
KNOWN_DISPATCH_TABLES = {
    "PLAYER_COLLISION_HANDLER_TABLE": 0x00001CBE,
    "ACTOR_COLLISION_HANDLER_TABLE": 0x00001EBA,
    "TERRAIN_RESPONSE_HANDLER_TABLE": 0x00004554,
}


def _scheduler_sequence(root: Path) -> tuple[int, list[dict[str, Any]]]:
    path = root / "re/scheduler/frame_phases.yml"
    if not path.is_file():
        return FRAME_LOOP, []
    document = load_yaml(path) or {}
    caller = parse_int(document.get("call_sequence_caller_entry", FRAME_LOOP))
    rows = []
    for raw in document.get("call_sequence", []) or []:
        if not isinstance(raw, dict):
            continue
        try:
            rows.append({
                "ordinal": parse_int(raw["ordinal"]),
                "call_site": parse_int(raw["call_site"]),
                "entry": parse_int(raw["entry"]),
            })
        except (KeyError, TypeError, ValueError):
            continue
    return caller, rows


def _record_address(record: dict[str, Any], key: str) -> int | None:
    value = record.get(key)
    if value is None:
        return None
    try:
        return parse_int(value)
    except (TypeError, ValueError):
        return None


def _values(database: AnalysisDatabase, filename: str, key: str) -> list[Any]:
    document = database.load(filename)
    if isinstance(document, dict):
        value = document.get(key, [])
    else:
        value = document
    return list(value) if isinstance(value, list) else []


def _validate_layout_coverage(database: AnalysisDatabase, root: Path, errors: list[str]) -> None:
    """Validate the normalized coverage view without requiring a layout file."""

    # Import lazily to keep the Ghidra database reader usable on its own and
    # to avoid a package cycle while the layout service imports that reader.
    from genie.layout.classifier import build_layout
    from genie.layout.validate import validate_layout

    try:
        layout = build_layout(database, root=root)
    except (OSError, TypeError, ValueError) as error:
        errors.append(f"could not derive normalized ROM layout: {error}")
        return
    errors.extend(validate_layout(layout))


def validate_database(
    database: AnalysisDatabase | Path | None = None,
    *,
    root: Path = ROOT,
) -> list[str]:
    """Return trust-gate failures for a generated whole-ROM database.

    The checks are deliberately tied to facts already tracked in the
    repository: the recovered scheduler sequence, canonical symbols, and the
    independently decoded dispatch-table references.  This makes the command
    useful after every fresh ``ghidra scan`` without making Ghidra's project
    or auto-generated names part of the contract.
    """

    if database is None:
        database = AnalysisDatabase()
    elif not isinstance(database, AnalysisDatabase):
        database = AnalysisDatabase(Path(database))
    root = Path(root).resolve()
    errors: list[str] = []

    try:
        metadata = database.metadata
        declared_files = tuple(str(filename) for filename in metadata.get("files", ()) or ())
        for filename in REQUIRED_FILES:
            if filename not in declared_files:
                errors.append(f"metadata does not declare required file {filename}")
            database.load(str(filename))
    except (OSError, ValueError, TypeError) as error:
        errors.append(f"database files are not readable: {error}")
        return errors

    if metadata.get("format") != EXPECTED_FORMAT:
        errors.append(f"metadata format is {metadata.get('format')!r}, expected {EXPECTED_FORMAT!r}")

    counts = metadata.get("counts", {})
    if isinstance(counts, dict):
        count_sources = {
            "functions": len(database.functions),
            "callgraph_edges": len(database.callgraph()),
            "xrefs": len(_values(database, "xrefs.json", "references")),
            "memory_reads": len(_values(database, "memory_reads.json", "references")),
            "memory_writes": len(_values(database, "memory_writes.json", "references")),
            "indirect_calls": len(_values(database, "indirect_calls.json", "references")),
            "jump_tables": len(_values(database, "jump_tables.json", "tables")),
            "address_classes": len(_values(database, "address_classes.json", "classes")),
        }
        for key, actual_count in count_sources.items():
            if key not in counts:
                continue
            try:
                matches = parse_int(counts[key]) == actual_count
            except (TypeError, ValueError):
                matches = False
            if not matches:
                errors.append(f"metadata count {key} is {counts[key]!r}, expected {actual_count}")

    try:
        _, expected_rom, _ = rom_entries()
        expected_size = parse_int(expected_rom["size"])
    except (KeyError, TypeError, ValueError, OSError):
        expected_size = None
    if expected_size is not None and metadata.get("rom_size") is not None:
        try:
            if parse_int(metadata["rom_size"]) != expected_size:
                errors.append(
                    f"ROM size is {metadata['rom_size']!r}, expected {expected_size}"
                )
        except (TypeError, ValueError):
            errors.append(f"invalid ROM size in metadata: {metadata['rom_size']!r}")

    functions = database.functions
    by_address = {_record_address(item, "address"): item for item in functions}
    by_name = {str(item.get("name")): item for item in functions}
    for name, address in KNOWN_FUNCTIONS.items():
        function = by_address.get(address)
        if function is None:
            errors.append(f"missing known function {name} at 0x{address:08X}")
        elif str(function.get("name")) != name:
            errors.append(
                f"function at 0x{address:08X} is {function.get('name')!r}, expected {name!r}"
            )
        if by_name.get(name) is not None and _record_address(by_name[name], "address") != address:
            errors.append(f"known function name {name} resolves to the wrong address")

    caller, sequence = _scheduler_sequence(root)
    if not sequence:
        errors.append("scheduler call sequence is missing or empty")
    elif len(sequence) != 37:
        errors.append(f"scheduler call sequence has {len(sequence)} ordinals, expected 37")
    if sequence:
        expected_sites = {row["call_site"] for row in sequence}
        # Ghidra's reference source for a JSR can be the preceding instruction
        # boundary, while the scheduler ledger records the call-site label.
        # Bound the scan by the recovered sequence and compare the ordered
        # targets, which is the actual scheduler invariant.
        site_min = min(expected_sites) - 4
        site_max = max(expected_sites)
        actual = [
            edge for edge in database.callgraph()
            if _record_address(edge, "from") == caller
            and site_min <= (_record_address(edge, "site") or -1) <= site_max
        ]
        if len(actual) != len(sequence):
            errors.append(
                f"direct scheduler calls indexed by the recovered call sites: {len(actual)}, expected {len(sequence)}"
            )
        actual.sort(key=lambda edge: (_record_address(edge, "site") or -1, _record_address(edge, "to") or -1))
        actual_targets = [_record_address(edge, "to") for edge in actual]
        expected_targets = [row["entry"] for row in sequence]
        for row, actual_target, expected_target in zip(sequence, actual_targets, expected_targets):
            if actual_target != expected_target:
                errors.append(
                    f"scheduler ordinal {row['ordinal']} targets "
                    f"0x{actual_target or 0:08X}, expected 0x{expected_target:08X}"
                )
        ordinal_30 = next((row for row in sequence if row["ordinal"] == 30), None)
        if ordinal_30 is None or ordinal_30["entry"] != KNOWN_FUNCTIONS["AnimationVM_TickActors"]:
            errors.append("scheduler ordinal 30 does not target AnimationVM_TickActors at 0x001AC784")

    writers = database.writers(FRAME_PHASE_COUNTER)
    exact_writer = any(
        _record_address(record, "from") == FRAME_PHASE_COUNTER_WRITER
        or _record_address(record, "site") == FRAME_PHASE_COUNTER_WRITER
        for record in writers
    )
    # The current exporter reports the address of the instruction's first
    # byte.  Preserve the same fact when Ghidra represents the addq as
    # 0x001A8C16 and carries the operand text in the reference record.
    exporter_writer = any(
        _record_address(record, "from") == FRAME_LOOP
        and str(record.get("instruction", "")).lower().find("0x00ff7e28") >= 0
        for record in writers
    )
    if not exact_writer and not exporter_writer:
        errors.append(
            "no writer for FRAME_PHASE_COUNTER at 0x001A8C1E "
            "(or equivalent addq reference from 0x001A8C16)"
        )

    xrefs = _values(database, "xrefs.json", "references")
    for name, address in KNOWN_DISPATCH_TABLES.items():
        if not any(_record_address(record, "to") == address for record in xrefs):
            errors.append(f"missing xref evidence for known dispatch table {name} at 0x{address:08X}")

    jump_tables = database.load("jump_tables.json")
    if isinstance(jump_tables, dict) and jump_tables.get("errors"):
        errors.append(f"Ghidra jump-table recovery reported {len(jump_tables['errors'])} error(s)")

    try:
        symbols = SymbolStore(root=root)
        symbol_errors = symbols.validate(rom_size=expected_size)
        errors.extend(f"canonical symbols: {error}" for error in symbol_errors)
        for symbol in symbols.symbols:
            if symbol.kind != "function":
                continue
            function = by_address.get(symbol.address)
            if function is None:
                errors.append(
                    f"tracked function {symbol.name} at 0x{symbol.address:08X} is absent from functions.json"
                )
            elif str(function.get("name")) != symbol.name:
                errors.append(
                    f"tracked function 0x{symbol.address:08X} exported as {function.get('name')!r}, "
                    f"expected {symbol.name!r}"
                )
    except (OSError, TypeError, ValueError) as error:
        errors.append(f"canonical symbols could not be validated: {error}")

    for item in _values(database, "address_classes.json", "classes"):
        try:
            start = parse_int(item["start"])
            end = parse_int(item["end"])
        except (KeyError, TypeError, ValueError):
            errors.append(f"invalid address-class range: {item!r}")
            continue
        if start < 0 or end < start or end > 0xFFFFFF:
            errors.append(f"address-class range outside 24-bit address space: 0x{start:X}-0x{end:X}")

    _validate_layout_coverage(database, root, errors)
    return errors


__all__ = ["validate_database"]
