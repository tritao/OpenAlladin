"""Tracked reverse-engineering knowledge and workspace status services."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from genie.common import load_yaml, normalize_types
from genie.runtime import *
from genie.symbols import SymbolStore
def _field_size(type_name: str) -> int:
    normalized = type_name.strip().lower()
    if normalized in {"u8", "i8", "s8", "byte", "bool"}:
        return 1
    if normalized in {"u16", "i16", "s16", "word"}:
        return 2
    if normalized in {"u32", "i32", "s32", "long", "rom_pointer", "pointer"}:
        return 4
    if normalized.startswith("u8[") and normalized.endswith("]"):
        return int(normalized[3:-1], 0)
    if normalized.startswith("u16[") and normalized.endswith("]"):
        return 2 * int(normalized[4:-1], 0)
    if normalized.startswith("u32[") and normalized.endswith("]"):
        return 4 * int(normalized[4:-1], 0)
    raise ValueError(f"unknown field type {type_name!r}")

def validate_knowledge(rom: Path) -> list[str]:
    errors: list[str] = []
    try:
        symbol_store = SymbolStore()
    except (OSError, ValueError) as error:
        return [f"symbols: {error}"]
    errors.extend(symbol_store.validate(rom_size=rom.stat().st_size))

    for type_path in sorted((ROOT / "re/types").glob("*.yml")):
        try:
            definition = load_yaml(type_path) or {}
            size = parse_int(definition.get("size", 0))
            occupied: dict[int, str] = {}
            for raw_offset, field in (definition.get("fields") or {}).items():
                offset = parse_int(raw_offset)
                width = _field_size(str((field or {}).get("type", "")))
                if offset < 0 or offset + width > size:
                    errors.append(f"{type_path.relative_to(ROOT)} field at 0x{offset:X} exceeds size 0x{size:X}")
                for byte in range(offset, offset + width):
                    previous = occupied.get(byte)
                    if previous:
                        errors.append(f"{type_path.relative_to(ROOT)} fields overlap at byte 0x{byte:X}: {previous}")
                    occupied[byte] = str((field or {}).get("name", raw_offset))
        except (OSError, TypeError, ValueError) as error:
            errors.append(f"{type_path.relative_to(ROOT)}: {error}")

    for actor_path in sorted((ROOT / "re/actors").glob("*.tsv")):
        current_frame: int | None = None
        seen_slots: set[tuple[int | None, int]] = set()
        records = 0
        try:
            for line_number, line in enumerate(actor_path.read_text(encoding="utf-8").splitlines(), 1):
                stripped = line.strip()
                if not stripped or stripped.startswith("#"):
                    continue
                fields = stripped.split()
                if fields[0] == "@frame":
                    if len(fields) != 2:
                        errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: invalid frame marker")
                        continue
                    current_frame = int(fields[1], 0)
                    continue
                if len(fields) < 8 or len(fields) > 16:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: expected 8..16 actor fields")
                    continue
                if "timeline" in actor_path.name and current_frame is None:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: record precedes frame marker")
                values = [int(value, 0) for value in fields]
                slot, actor_type, x, y, movement_pc, frame_ptr, animation_pc, flags = values[:8]
                if not 0 <= slot < 32:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: slot outside 0..31")
                for name, value, maximum in (
                    ("type", actor_type, 0xFF),
                    ("x", x, 0xFFFF),
                    ("y", y, 0xFFFF),
                    ("movement_pc", movement_pc, 0xFFFFFF),
                    ("frame_ptr", frame_ptr, 0xFFFFFFFF),
                    ("animation_pc", animation_pc, 0xFFFFFFFF),
                    ("flags", flags, 0xFF),
                ):
                    if not 0 <= value <= maximum:
                        errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: {name} outside range")
                if len(values) >= 9 and not 0 <= values[8] <= 0xFF:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: facing_x_flip outside range")
                if len(values) >= 10 and not 0 <= values[9] <= 0xFF:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: facing_y_flip outside range")
                if len(values) >= 11 and not 0 <= values[10] <= 0xFF:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: movement_command_timer outside range")
                if len(values) >= 12 and not 0 <= values[11] <= 0xFFFFFFFF:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: movement_loop_pc outside range")
                if len(values) >= 13 and not 0 <= values[12] <= 0xFF:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: movement_loop_timer outside range")
                if len(values) >= 14 and not 0 <= values[13] <= 0xFFFFFFFF:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: movement_return_pc outside range")
                if len(values) >= 15 and not 0 <= values[14] <= 0xFFFF:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: movement_word_18 outside range")
                if len(values) >= 16 and not 0 <= values[15] <= 0xFFFF:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: movement_word_1a outside range")
                key = (current_frame, slot)
                if key in seen_slots:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: duplicate slot {slot}")
                seen_slots.add(key)
                records += 1
            if records == 0:
                errors.append(f"{actor_path.relative_to(ROOT)}: no actor records")
        except (OSError, TypeError, ValueError) as error:
            errors.append(f"{actor_path.relative_to(ROOT)}: {error}")
    return errors

def asset_current(rom: Path) -> bool:
    manifest = ROOT / "build/assets/manifest.json"
    if not manifest.is_file():
        return False
    try:
        identity = json.loads(manifest.read_text(encoding="utf-8")).get("rom", {})
        actual = hashes(rom)
        return all(identity.get(key) == actual[key] for key in ("size", "crc32", "sha1", "sha256"))
    except (OSError, ValueError, KeyError):
        return False

def project_current(rom: Path) -> bool:
    try:
        config = load_yaml(GHIDRA_CONFIG) or {}
        project_dir = ROOT / config["ghidra"]["project_dir"]
        project_name = config["ghidra"]["project_name"]
        analysis_path = ROOT / "build/re/analysis.json"
        if not (analysis_path.is_file() and (project_dir / f"{project_name}.gpr").is_file()):
            return False
        analysis = json.loads(analysis_path.read_text(encoding="utf-8"))
        return analysis.get("rom_identity", {}).get("sha256") == hashes(rom)["sha256"]
    except (OSError, KeyError, TypeError, ValueError):
        return False

def generated_opcode_count(path: Path, *, movement: bool) -> tuple[int, int]:
    total = 0x15
    if not path.is_file():
        return 0, total
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
        found: set[str] = set()
        for stream in (document.get("streams") or {}).values():
            if movement:
                for step in stream.get("steps", []):
                    found.update(command.get("opcode") for command in step.get("commands", []) if command.get("opcode"))
            else:
                found.update(instruction.get("opcode") for instruction in stream.get("instructions", []) if instruction.get("kind") == "command")
        return len(found), total
    except (OSError, TypeError, ValueError, json.JSONDecodeError):
        return 0, total

def print_status(rom: Path) -> int:
    default_name, expected, _ = rom_entries()
    actual = hashes(rom) if rom.is_file() else None
    rom_ok = bool(actual and all(str(expected.get(key, "")).upper() == str(actual[key]).upper() for key in ("size", "crc32", "sha1", "sha256")))
    knowledge_errors = validate_knowledge(rom) if rom.is_file() else ["ROM not found"]
    ghidra = load_yaml(GHIDRA_CONFIG) or {}
    install = ROOT / ghidra.get("ghidra", {}).get("install_dir", ".tools/ghidra")
    ghidra_ok = (install / "support" / ("pyghidraRun.bat" if os.name == "nt" else "pyghidraRun")).is_file()
    animation_count, animation_total = generated_opcode_count(ROOT / "build/re/animation_streams.json", movement=False)
    movement_count, movement_total = generated_opcode_count(ROOT / "build/re/movement_streams.json", movement=True)
    try:
        functions = len(load_yaml(ROOT / "re/symbols/functions.yml") or {})
        confirmed_ram = sum(1 for value in (load_yaml(ROOT / "re/symbols/ram.yml") or {}).values() if (value or {}).get("confidence") == "confirmed")
        actor = load_yaml(ROOT / "re/types/actor.yml") or {}
        actor_fields = len(actor.get("fields") or {})
    except (OSError, TypeError, ValueError):
        functions = confirmed_ram = actor_fields = 0

    print(f"ROM               {'OK' if rom_ok else 'MISSING/MISMATCH'}")
    print(f"Ghidra             {'OK' if ghidra_ok else 'missing'}")
    print(f"Ghidra project     {'current' if project_current(rom) else 'stale/missing'}")
    print(f"symbols            {'valid' if not knowledge_errors else f'invalid ({len(knowledge_errors)})'}")
    print(f"types              {'valid' if not knowledge_errors else f'check validate ({len(knowledge_errors)})'}")
    print(f"assets             {'current' if rom.is_file() and asset_current(rom) else 'missing/stale'}")
    mame = ROOT / "external/mame/mame"
    print(f"MAME               {'available' if mame.is_file() and os.access(mame, os.X_OK) else 'missing'}")
    print()
    print(f"Known functions     {functions}")
    print(f"Confirmed RAM       {confirmed_ram}")
    print(f"Actor fields        {actor_fields} named / {parse_int(actor.get('size', 0)) if actor else 0} bytes")
    print(f"Animation opcodes   {animation_count}/{animation_total}")
    print(f"Movement opcodes    {movement_count}/{movement_total}")
    coverage_path = ROOT / "build/re/coverage.json"
    try:
        coverage = json.loads(coverage_path.read_text(encoding="utf-8"))
        summary = coverage.get("summary") or {}
        print(
            f"Runtime coverage    {summary.get('unique_pc_count', 0)} PCs / "
            f"{summary.get('scenario_count', 0)} scenarios / "
            f"{summary.get('unique_edge_count', 0)} edges"
        )
    except (OSError, TypeError, ValueError, json.JSONDecodeError):
        print("Runtime coverage    missing")
    if actual is None:
        print(f"ROM identity        {default_name}: unavailable")
    else:
        print(f"ROM identity        {actual['sha1']}")
    return 0 if rom_ok and not knowledge_errors else 1

__all__ = [name for name in globals() if not name.startswith("__")]
