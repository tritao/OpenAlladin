from __future__ import annotations

import json

from genie.deasm import DeasmInput, DeasmError, InstructionRecord, collect, emit, validate_input
from genie.deasm.syntax import Gnu68000Syntax
from genie.layout.model import Layout, LayoutRange
from genie.symbols import Symbol, SymbolStore


def _fixture():
    rom = bytes([
        0x4E, 0x75, 0x4E, 0x71,
        0x04, 0x05, 0x06, 0x07,
        0x00, 0x01, 0x00, 0x02,
        0xAA, 0xBB, 0xCC, 0xDD,
        *range(0x10, 0x20),
    ])
    layout = Layout(
        rom_size=len(rom),
        ranges=(
            LayoutRange(0x00, 0x03, "CODE", "test", "FirstFunction"),
            LayoutRange(0x04, 0x07, "POINTER_TABLE", "test", "PointerTable"),
            LayoutRange(0x08, 0x0B, "JUMP_TABLE", "test"),
            LayoutRange(0x0C, 0x0F, "ANIMATION_STREAM", "test"),
            LayoutRange(0x10, 0x1F, "UNKNOWN", "test"),
        ),
    )
    instructions = (
        InstructionRecord(0x00, 2, b"\x4E\x75", "rts", function=0x00, function_name="FirstFunction"),
        InstructionRecord(0x02, 2, b"\x4E\x71", "nop", function=0x00, function_name="FirstFunction"),
    )
    symbols = SymbolStore(symbols=(
        Symbol(0x00, "FirstFunction", "function"),
        Symbol(0x04, "PointerTable", "data"),
    ))
    return DeasmInput(rom, layout, instructions), symbols


def test_deasm_emits_each_layout_byte_once_and_preserves_code_bytes():
    value, symbols = _fixture()

    assert validate_input(value) == []
    result = emit(value, symbols)

    assert result.owned_bytes == len(value.rom)
    assert "FirstFunction:\n    rts\n    nop" in result.source
    assert "PointerTable:\n    dc.l    $04050607" in result.source
    assert "JumpTable_00000008:" in result.source
    assert "UnknownData_00000010:" in result.source


def test_deasm_stats_separates_semantic_and_mechanical_labels():
    value, symbols = _fixture()

    stats = collect(value, symbols)

    assert stats.owned_bytes == len(value.rom)
    assert stats.unowned_bytes == 0
    assert stats.bytes_by_class["CODE"] == 4
    assert stats.bytes_by_class["UNKNOWN"] == 16
    assert stats.functions == 1
    assert stats.instructions == 2
    assert stats.semantic_names == 2
    assert stats.mechanical_names == 3
    assert "ROM                         32 bytes" in stats.render()

    measured = collect(
        value,
        symbols,
        metrics={"instructionized": 2, "raw_fallback": 1},
    )
    assert measured.instructionized == 2
    assert measured.raw_fallback == 1
    assert '"raw_fallback": 1' in json.dumps(measured.to_dict(), sort_keys=True)


def test_deasm_rejects_instruction_bytes_that_do_not_match_rom():
    value, _ = _fixture()
    bad = InstructionRecord(0x00, 2, b"\x4E\x74", "rts")
    errors = validate_input(DeasmInput(value.rom, value.layout, (bad,)))

    assert any("instruction bytes differ" in error for error in errors)
    try:
        emit(DeasmInput(value.rom, value.layout, (bad,)), SymbolStore(symbols=()))
    except DeasmError:
        pass
    else:
        raise AssertionError("expected byte mismatch to stop emission")


def test_deasm_input_round_trips_from_json(tmp_path):
    value, _ = _fixture()
    rom_path = tmp_path / "rom.bin"
    layout_path = tmp_path / "layout.json"
    instructions_path = tmp_path / "instructions.json"
    rom_path.write_bytes(value.rom)
    layout_path.write_text(json.dumps(value.layout.to_dict()), encoding="utf-8")
    instructions_path.write_text(json.dumps([
        {
            "address": "0x00000000",
            "size": 2,
            "bytes": "4E75",
            "mnemonic": "rts",
            "operands": "",
            "function": "0x00000000",
            "function_name": "FirstFunction",
            "references": [],
        },
        {
            "address": "0x00000002",
            "size": 2,
            "bytes": "4E71",
            "mnemonic": "nop",
            "operands": "",
            "function": "0x00000000",
            "function_name": "FirstFunction",
            "references": [],
        },
    ]), encoding="utf-8")

    from genie.deasm import load_input

    loaded = load_input(rom_path, layout_path, instructions_path)
    assert loaded.rom == value.rom
    assert loaded.instructions == value.instructions


def test_gnu_deasm_backend_normalizes_operands_and_targets():
    rom = b"\x66\x00\x4e\x75"
    layout = Layout(
        rom_size=len(rom),
        ranges=(LayoutRange(0x00, 0x03, "CODE", "test", "BranchFunction"),),
    )
    instructions = (
        InstructionRecord(
            0x00,
            2,
            rom[:2],
            "bne.b",
            "0x00000002",
            function=0x00,
            references=({"operand_index": 0, "to": "0x00000002"},),
        ),
        InstructionRecord(0x02, 2, rom[2:], "rts", function=0x00),
    )
    value = DeasmInput(rom, layout, instructions)
    result = emit(value, SymbolStore(symbols=(Symbol(0x00, "BranchFunction", "function"),)), syntax=Gnu68000Syntax())

    assert "bne.b Loc_00000002" in result.source
    assert "BranchFunction:" in result.source
    assert result.instructionized == 2
    assert result.raw_fallback == 0
