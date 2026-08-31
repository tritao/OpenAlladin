import json

from genie.ghidra.vm_writes import find_vm_writers


def test_find_vm_writers_decodes_ed_and_fa_global_commands(tmp_path):
    rom = bytearray(0x220)
    rom[0x100:0x106] = bytes.fromhex("ED01F1010001")
    rom[0x106:0x10C] = bytes.fromhex("FA81F1010002")
    rom_path = tmp_path / "rom.bin"
    rom_path.write_bytes(rom)
    layout_path = tmp_path / "layout.json"
    layout_path.write_text(json.dumps({
        "ranges": [{
            "start": "0x00000100",
            "end": "0x0000010B",
            "class": "ANIMATION_STREAM",
            "name": "TEST_STREAM",
        }]
    }))

    writers = find_vm_writers(rom_path, layout_path, 0x00FFF101)

    assert [(item["address"], item["operation"], item["value"]) for item in writers] == [
        ("0x00000100", "write", "0x01"),
        ("0x00000106", "subtract", "0x02"),
    ]
    assert all(item["stream"] == "TEST_STREAM" for item in writers)


def test_find_vm_writers_does_not_match_ed_inside_a_payload(tmp_path):
    rom = bytearray(0x220)
    rom[0x100:0x112] = bytes.fromhex(
        "ED02F101ED01"
        "ED01F1010002"
    )
    rom_path = tmp_path / "rom.bin"
    rom_path.write_bytes(rom)
    layout_path = tmp_path / "layout.json"
    layout_path.write_text(json.dumps({
        "ranges": [{
            "start": "0x00000100",
            "end": "0x00000111",
            "class": "ANIMATION_STREAM",
            "name": "TEST_STREAM",
        }]
    }))

    writers = find_vm_writers(rom_path, layout_path, 0x00FFF101)

    assert [(item["address"], item["value"]) for item in writers] == [
        ("0x00000100", "0xED01"),
        ("0x00000106", "0x02"),
    ]
