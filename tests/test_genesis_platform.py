from __future__ import annotations

from genie.platforms.genesis.input import (
    Genesis3ButtonInput,
    INPUT_BUTTONS,
    INPUT_MASKS,
    INPUT_MAPPING,
    buttons_for_mask,
    client_input_tokens,
    token_for_mask,
)
from genie.platforms.genesis.rom import has_genesis_header, is_genesis_rom
from genie.platforms.genesis.vdp import decode_tile, genesis_color, vdp_word


def test_genesis_input_preserves_runtime_contract():
    assert isinstance(Genesis3ButtonInput(), Genesis3ButtonInput)
    assert INPUT_BUTTONS == ("up", "down", "left", "right", "a", "b", "c", "start")
    assert INPUT_MASKS["a"] == 1 << 4
    assert INPUT_MAPPING == "mame-genesis-3button-v1"
    assert buttons_for_mask(0x50) == ["a", "c"]
    assert token_for_mask(0) == "none"
    assert client_input_tokens(None, ["a+b", "c", "right"]) == ["b+c", "a", "right"]
    assert client_input_tokens({"controller_mapping": INPUT_MAPPING}, ["a+b"]) == ["a+b"]


def test_genesis_vdp_helpers_are_platform_services():
    tile_data = bytes(range(32))
    decoded = decode_tile(tile_data, 0)
    assert len(decoded) == 8
    assert decoded[0] == [0, 0, 0, 1, 0, 2, 0, 3]
    assert genesis_color(0xEEE) == (255, 255, 255, 255)
    assert vdp_word(0xE123) == {
        "raw": 0xE123,
        "tile": 0x123,
        "palette": 3,
        "priority": True,
        "hflip": False,
        "vflip": False,
    }


def test_genesis_header_helpers():
    rom = bytearray(0x104)
    rom[0x100:0x104] = b"SEGA"
    assert has_genesis_header(bytes(rom))
    assert is_genesis_rom(bytes(rom))
    assert not has_genesis_header(bytes(rom[:0x100]))
