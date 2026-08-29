"""Genesis tile, palette, and VDP word helpers."""

from __future__ import annotations

from pathlib import Path

from genie.assets.png import write_ppm, write_rgba


TILE_BYTES = 32


def decode_tile(tile_data: bytes, tile_index: int) -> list[list[int]]:
    start = tile_index * TILE_BYTES
    if start < 0 or start + TILE_BYTES > len(tile_data):
        raise ValueError(f"tile {tile_index} is outside {len(tile_data)} bytes of character data")
    pixels = [[0] * 8 for _ in range(8)]
    for y in range(8):
        row = start + y * 4
        for x in range(8):
            value = tile_data[row + (x >> 1)]
            pixels[y][x] = (value >> 4) & 0xF if not (x & 1) else value & 0xF
    return pixels


def genesis_color(word: int) -> tuple[int, int, int, int]:
    # Match MAME's Sega VDP DAC table rather than linearly stretching the
    # three-bit CRAM channels. This is observable in captured screenshots.
    levels = (0, 52, 87, 116, 144, 172, 206, 255)
    return tuple(levels[(word >> shift) & 7] for shift in (1, 5, 9)) + (255,)


def decode_palette(data: bytes, palette_index: int = 0) -> list[tuple[int, int, int, int]]:
    start = palette_index * 32
    if start + 32 > len(data):
        raise ValueError(f"palette {palette_index} is outside {len(data)} bytes")
    return [genesis_color(int.from_bytes(data[start + i:start + i + 2], "big")) for i in range(0, 32, 2)]


def vdp_word(word: int) -> dict[str, int | bool]:
    return {
        "raw": word,
        "tile": word & 0x7FF,
        "palette": (word >> 13) & 3,
        "priority": bool(word & 0x8000),
        "hflip": bool(word & 0x0800),
        "vflip": bool(word & 0x1000),
    }


def render_tilemap(
    tile_data: bytes,
    words: list[int],
    width: int,
    height: int,
    palettes: bytes,
    output: Path,
    default_palette: int = 0,
    ppm_output: Path | None = None,
) -> None:
    if len(words) < width * height:
        raise ValueError("tilemap is shorter than its declared dimensions")
    all_palettes = [decode_palette(palettes, index) for index in range(len(palettes) // 32)]
    if not all_palettes:
        all_palettes = [[(0, 0, 0, 255)] * 16]
    pixels = bytearray(width * 8 * height * 8 * 4)
    out_width = width * 8
    for map_y in range(height):
        for map_x in range(width):
            info = vdp_word(words[map_y * width + map_x])
            tile = decode_tile(tile_data, int(info["tile"]))
            palette = all_palettes[int(info["palette"]) % len(all_palettes)] if palettes else all_palettes[default_palette]
            for y in range(8):
                source_y = 7 - y if info["vflip"] else y
                for x in range(8):
                    source_x = 7 - x if info["hflip"] else x
                    color = palette[tile[source_y][source_x]]
                    dest = ((map_y * 8 + y) * out_width + map_x * 8 + x) * 4
                    pixels[dest:dest + 4] = bytes(color)
    write_rgba(output, out_width, height * 8, pixels)
    if ppm_output is not None:
        write_ppm(ppm_output, out_width, height * 8, pixels)


def render_tileset(tile_data: bytes, palette_data: bytes, output: Path, columns: int = 16) -> None:
    tile_count = len(tile_data) // TILE_BYTES
    rows = (tile_count + columns - 1) // columns
    palettes = decode_palette(palette_data, 0) if len(palette_data) >= 32 else [(0, 0, 0, 255)] * 16
    pixels = bytearray(columns * 8 * rows * 8 * 4)
    width = columns * 8
    for index in range(tile_count):
        tile = decode_tile(tile_data, index)
        tx, ty = index % columns, index // columns
        for y in range(8):
            for x in range(8):
                dest = ((ty * 8 + y) * width + tx * 8 + x) * 4
                pixels[dest:dest + 4] = bytes(palettes[tile[y][x]])
    write_rgba(output, width, rows * 8, pixels)


__all__ = [
    "TILE_BYTES",
    "decode_palette",
    "decode_tile",
    "genesis_color",
    "render_tilemap",
    "render_tileset",
    "vdp_word",
]
