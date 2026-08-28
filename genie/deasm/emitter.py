"""Deterministic whole-ROM source emission from the offline analysis inputs."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from genie.layout.model import Layout, LayoutRange
from genie.layout.validate import validate_layout
from genie.symbols import SymbolStore

from .labels import range_labels
from .model import DeasmInput, InstructionRecord
from .operands import render_bytes, render_instruction, render_longs


class DeasmError(ValueError):
    """Raised when a deasm input cannot be consumed safely."""


@dataclass(frozen=True, slots=True)
class EmissionResult:
    source: str
    owned_bytes: int
    labels: dict[int, str]


def _document_records(document: object, key: str) -> list[dict]:
    if isinstance(document, dict):
        document = document.get(key, [])
    if not isinstance(document, list):
        raise DeasmError(f"{key} document must contain a list")
    return [dict(item) for item in document]


def load_input(rom_path: Path, layout_path: Path, instructions_path: Path) -> DeasmInput:
    """Load and validate the files required by ``deasm build`` and ``stats``."""

    rom_path = Path(rom_path)
    layout_path = Path(layout_path)
    instructions_path = Path(instructions_path)
    try:
        rom = rom_path.read_bytes()
    except OSError as error:
        raise DeasmError(f"could not read ROM {rom_path}: {error}") from error
    try:
        layout = Layout.from_dict(json.loads(layout_path.read_text(encoding="utf-8")))
    except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError) as error:
        raise DeasmError(f"could not read layout {layout_path}: {error}") from error
    try:
        document = json.loads(instructions_path.read_text(encoding="utf-8"))
        instructions = tuple(
            sorted(
                (InstructionRecord.from_dict(item) for item in _document_records(document, "instructions")),
                key=lambda item: item.address,
            )
        )
    except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError) as error:
        raise DeasmError(f"could not read instructions {instructions_path}: {error}") from error

    deasm_input = DeasmInput(rom=rom, layout=layout, instructions=instructions)
    errors = validate_input(deasm_input)
    if errors:
        raise DeasmError("deasm input validation failed:\n" + "\n".join(f"- {error}" for error in errors))
    return deasm_input


def validate_input(value: DeasmInput) -> list[str]:
    """Check layout coverage and that exported instruction bytes match the ROM."""

    errors = list(validate_layout(value.layout))
    if len(value.rom) != value.layout.rom_size:
        errors.append(
            f"ROM has {len(value.rom)} bytes, layout declares {value.layout.rom_size}"
        )
    previous_end = -1
    for instruction in value.instructions:
        if instruction.size <= 0:
            errors.append(f"instruction at 0x{instruction.address:08X} has non-positive size")
            continue
        if len(instruction.encoding) != instruction.size:
            errors.append(
                f"instruction at 0x{instruction.address:08X} has {len(instruction.encoding)} encoded bytes, "
                f"expected {instruction.size}"
            )
        if instruction.address < 0 or instruction.end >= len(value.rom):
            errors.append(
                f"instruction outside ROM: 0x{instruction.address:08X}-0x{instruction.end:08X}"
            )
            continue
        if instruction.address <= previous_end:
            errors.append(f"overlapping instruction at 0x{instruction.address:08X}")
        previous_end = max(previous_end, instruction.end)
        actual = value.rom[instruction.address:instruction.end + 1]
        if actual != instruction.encoding:
            errors.append(
                f"instruction bytes differ at 0x{instruction.address:08X}: "
                f"database {instruction.encoding.hex().upper()}, ROM {actual.hex().upper()}"
            )
        if not instruction.mnemonic.strip():
            errors.append(f"instruction at 0x{instruction.address:08X} has no mnemonic")
    return errors


def _emit_span(lines: list[str], emitted: list[tuple[int, int]], start: int, end: int, data: bytes, *, directive: str = "dc.b") -> None:
    if start > end:
        return
    if len(data) != end - start + 1:
        raise DeasmError(
            f"emitter span length mismatch at 0x{start:08X}: {len(data)} bytes for {end - start + 1}"
        )
    if emitted and start <= emitted[-1][1]:
        raise DeasmError(f"emitter attempted to own overlapping bytes at 0x{start:08X}")
    lines.extend(render_bytes(data, directive=directive))
    emitted.append((start, end))


def _emit_code(
    lines: list[str],
    emitted: list[tuple[int, int]],
    item: LayoutRange,
    rom: bytes,
    instructions: Iterable[InstructionRecord],
) -> None:
    cursor = item.start
    selected = [
        instruction
        for instruction in instructions
        if item.start <= instruction.address <= item.end and instruction.end <= item.end
    ]
    for instruction in selected:
        if instruction.address > cursor:
            _emit_span(lines, emitted, cursor, instruction.address - 1, rom[cursor:instruction.address])
        if instruction.address < cursor:
            raise DeasmError(f"instruction ordering crossed at 0x{instruction.address:08X}")
        lines.append(render_instruction(instruction.mnemonic, instruction.operands))
        emitted.append((instruction.address, instruction.end))
        cursor = instruction.end + 1
    if cursor <= item.end:
        _emit_span(lines, emitted, cursor, item.end, rom[cursor:item.end + 1])


def _emit_table(
    lines: list[str],
    emitted: list[tuple[int, int]],
    item: LayoutRange,
    rom: bytes,
) -> None:
    complete_end = item.start + (item.size // 4) * 4 - 1
    if complete_end >= item.start:
        data = rom[item.start:complete_end + 1]
        lines.extend(render_longs(data))
        emitted.append((item.start, complete_end))
    if complete_end < item.end:
        start = complete_end + 1
        _emit_span(lines, emitted, start, item.end, rom[start:item.end + 1])


def emit(value: DeasmInput, symbols: SymbolStore) -> EmissionResult:
    """Emit one source range for every byte in a validated ROM layout."""

    errors = validate_input(value)
    if errors:
        raise DeasmError("deasm input validation failed:\n" + "\n".join(f"- {error}" for error in errors))

    labels = range_labels(value.layout.ranges, symbols)
    instruction_map = tuple(value.instructions)
    lines = [
        "; Generated by genie deasm build; source ROM bytes remain local.",
        "; Every byte is emitted exactly once according to the validated layout.",
        "",
    ]
    emitted: list[tuple[int, int]] = []
    for item in value.layout.ranges:
        lines.append(
            "; 0x{:08X}-0x{:08X} {} ({})".format(
                item.start, item.end, item.layout_class, item.source
            )
        )
        lines.append(f"{labels[item.start]}:")
        if item.layout_class == "CODE":
            _emit_code(lines, emitted, item, value.rom, instruction_map)
        elif item.layout_class in {"JUMP_TABLE", "POINTER_TABLE", "SCENE_TABLE"}:
            _emit_table(lines, emitted, item, value.rom)
        else:
            _emit_span(lines, emitted, item.start, item.end, value.rom[item.start:item.end + 1])
        lines.append("")

    owned_bytes = sum(end - start + 1 for start, end in emitted)
    if not emitted or emitted[0][0] != 0 or emitted[-1][1] != len(value.rom) - 1:
        raise DeasmError("emitted byte ownership does not cover the complete ROM")
    if any(next_left != right + 1 for (_, right), (next_left, _) in zip(emitted, emitted[1:])):
        raise DeasmError("emitted byte ownership is not contiguous")
    if owned_bytes != len(value.rom):
        raise DeasmError(f"emitter owns {owned_bytes} bytes, expected {len(value.rom)}")
    return EmissionResult("\n".join(lines).rstrip() + "\n", owned_bytes, labels)


__all__ = ["DeasmError", "EmissionResult", "emit", "load_input", "validate_input"]
