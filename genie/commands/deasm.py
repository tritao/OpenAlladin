"""Command-line boundary for offline ROM deassembly generation."""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass

from genie.deasm import (
    AssemblyError,
    DeasmError,
    ToolchainError,
    assemble,
    collect,
    emit,
    find_toolchain,
    load_input,
)
from genie.deasm.syntax import Gnu68000Syntax
from genie.deasm.model import DeasmInput, InstructionRecord
from genie.runtime import ROOT, resolve
from genie.symbols import SymbolStore


DEFAULT_DATABASE = ROOT / "build/re/full-rom"
DEFAULT_OUTPUT = ROOT / "build/re/deasm/aladdin.asm"


def _input(args: argparse.Namespace):
    database = resolve(args.database)
    layout = resolve(args.layout) if args.layout else database / "layout.json"
    instructions = resolve(args.instructions) if args.instructions else database / "instructions.json"
    return load_input(resolve(args.rom), layout, instructions)


def _symbols() -> SymbolStore:
    return SymbolStore(root=ROOT)


@dataclass(frozen=True, slots=True)
class _ExactBuild:
    source: str
    rebuilt: bytes
    instructionized: int
    raw_fallback: int
    attempts: int


def _first_difference(original: bytes, rebuilt: bytes) -> int | None:
    for address in range(max(len(original), len(rebuilt))):
        left = original[address] if address < len(original) else None
        right = rebuilt[address] if address < len(rebuilt) else None
        if left != right:
            return address
    return None


def _hex_window(value: bytes, address: int) -> str:
    return " ".join(f"{byte:02X}" for byte in value[address:address + 8]) or "<none>"


def _instruction_for_line(result, line_number: int | None) -> int | None:
    if line_number is None:
        return None
    return next(
        (address for address, line in result.instruction_lines.items() if line == line_number),
        None,
    )


def _probe_source(source: str, instruction_lines: dict[int, int]) -> tuple[str, dict[int, int]]:
    """Add private labels used to measure assembler instruction positions."""

    address_by_line = {line: address for address, line in instruction_lines.items()}
    result: list[str] = []
    line_addresses: dict[int, int] = {}
    for line_number, line in enumerate(source.splitlines(), 1):
        address = address_by_line.get(line_number)
        if address is not None:
            result.append(f"I_{address:08X}:")
        result.append(line)
        if address is not None:
            line_addresses[len(result)] = address
    return "\n".join(result) + "\n", line_addresses


def _first_position_drift(
    value: DeasmInput,
    symbols: dict[str, int],
    instruction_lines: dict[int, int],
) -> tuple[InstructionRecord, ...]:
    """Return instructions whose encoded sizes change source layout."""

    previous_delta = 0
    result: list[InstructionRecord] = []
    instructions = tuple(
        instruction for instruction in value.instructions if instruction.address in instruction_lines
    )
    for index, instruction in enumerate(instructions):
        actual = symbols.get(f"I_{instruction.address:08X}")
        if actual is None:
            raise DeasmError(
                f"assembler probe did not export position label I_{instruction.address:08X}"
            )
        delta = actual - instruction.address
        if delta != previous_delta:
            result.append(instructions[index - 1] if index else instruction)
        previous_delta = delta
    return tuple(result)


def _instruction_for_address(
    value: DeasmInput,
    address: int,
    excluded: set[int],
    available: set[int] | None = None,
) -> InstructionRecord | None:
    return next(
        (
            instruction
            for instruction in value.instructions
            if instruction.address not in excluded
            and (available is None or instruction.address in available)
            and instruction.address <= address <= instruction.end
        ),
        None,
    )


def _source_context(source: str, line_number: int | None) -> str:
    if line_number is None:
        return ""
    lines = source.splitlines()
    start = max(1, line_number - 1)
    end = min(len(lines), line_number + 1)
    return "\n".join(
        f"{line:>6}{' >' if line == line_number else '  '} {lines[line - 1]}"
        for line in range(start, end + 1)
    )


def _build_exact(value: DeasmInput, symbols: SymbolStore) -> _ExactBuild:
    """Render and assemble, isolating instructions that do not round-trip."""

    toolchain = find_toolchain(ROOT)
    syntax = Gnu68000Syntax()
    force_raw: set[int] = set()
    max_attempts = len(value.instructions) + 1
    for attempt in range(1, max_attempts + 1):
        result = emit(value, symbols, syntax=syntax, force_raw=force_raw)
        probe_source, probe_lines = _probe_source(result.source, result.instruction_lines)
        try:
            probe = assemble(probe_source, toolchain, symbol_prefix="I_")
        except AssemblyError as error:
            address = probe_lines.get(error.line_number or -1)
            if address is None or address in force_raw:
                context = _source_context(probe_source, error.line_number)
                detail = f"assembler rejected generated source:\n{error}"
                if context:
                    detail += f"\n\nsource context:\n{context}"
                raise DeasmError(detail) from error
            force_raw.add(address)
            continue

        drift = _first_position_drift(value, probe.symbols or {}, result.instruction_lines)
        if drift:
            force_raw.update(instruction.address for instruction in drift)
            continue

        try:
            rebuilt = assemble(result.source, toolchain).binary
        except AssemblyError as error:
            address = _instruction_for_line(result, error.line_number)
            if address is None or address in force_raw:
                context = _source_context(result.source, error.line_number)
                detail = f"assembler rejected generated source:\n{error}"
                if context:
                    detail += f"\n\nsource context:\n{context}"
                raise DeasmError(detail) from error
            force_raw.add(address)
            continue

        mismatch = _first_difference(value.rom, rebuilt)
        if mismatch is None:
            return _ExactBuild(
                result.source,
                rebuilt,
                result.instructionized,
                result.raw_fallback,
                attempt,
            )

        instruction = _instruction_for_address(
            value,
            mismatch,
            force_raw,
            set(result.instruction_lines),
        )
        if instruction is None:
            raise DeasmError(
                "assembler output does not match the ROM and no instruction can be isolated:\n"
                f"first mismatch: 0x{mismatch:08X}\n"
                f"original: {_hex_window(value.rom, mismatch)}\n"
                f"rebuilt:  {_hex_window(rebuilt, mismatch)}\n"
                f"rebuilt size: {len(rebuilt):,}; ROM size: {len(value.rom):,}"
            )
        force_raw.add(instruction.address)

    raise DeasmError(
        f"could not make generated source byte-exact after {max_attempts} attempts"
    )


def command_deasm_build(args: argparse.Namespace) -> int:
    try:
        value = _input(args)
        build = _build_exact(value, _symbols())
    except (DeasmError, OSError, TypeError, ValueError, ToolchainError) as error:
        print(f"ERROR {error}")
        return 1
    output = resolve(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(build.source, encoding="utf-8")
    print(
        f"Generated byte-exact ROM deassembly: {value.layout.rom_size:,} bytes -> {output} "
        f"({build.instructionized:,} instructions, {build.raw_fallback:,} raw fallbacks)"
    )
    return 0


def command_deasm_stats(args: argparse.Namespace) -> int:
    try:
        value = _input(args)
        stats = collect(value, _symbols())
    except (DeasmError, OSError, TypeError, ValueError) as error:
        print(f"ERROR {error}")
        return 1
    if args.json_output:
        print(json.dumps(stats.to_dict(), indent=2, sort_keys=True))
    else:
        print(stats.render())
    return 0


def command_deasm_verify(args: argparse.Namespace) -> int:
    try:
        value = _input(args)
        build = _build_exact(value, _symbols())
    except (DeasmError, OSError, TypeError, ValueError, ToolchainError) as error:
        print(f"ERROR {error}")
        return 1

    output = resolve(args.output)
    rebuilt_path = resolve(args.rebuilt)
    output.parent.mkdir(parents=True, exist_ok=True)
    rebuilt_path.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(build.source, encoding="utf-8")
    rebuilt_path.write_bytes(build.rebuilt)
    print(
        f"Verified byte-exact reassembly: {len(value.rom):,} / {len(value.rom):,} bytes match "
        f"(attempts {build.attempts}, raw fallbacks {build.raw_fallback:,})"
    )
    print(f"Source:  {output}")
    print(f"Rebuilt: {rebuilt_path}")
    return 0


__all__ = ["command_deasm_build", "command_deasm_stats", "command_deasm_verify"]
