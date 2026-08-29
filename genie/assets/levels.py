"""Aladdin Genesis level-table parsing and rendering."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from pathlib import Path
import struct
from typing import Any

from genie.platforms.genesis.vdp import render_tilemap
from .png import write_rgba
from .rnc import RncError, decompress_at, is_rnc


# This is the distinctive dampening table used by the original level loader.
# Keeping it as runs makes the scanner readable and avoids hard-coding the
# resulting ROM address.
_LEVEL_MARKER_RUNS = (
    (0, 9), (1, 8), (2, 7), (3, 6), (4, 5), (5, 5), (6, 5), (7, 6),
    (8, 6), (9, 14), (10, 14), (11, 14), (12, 16), (13, 24), (14, 36), (15, 36),
)
LEVEL_MARKER = b"".join(bytes((value,)) * count for value, count in _LEVEL_MARKER_RUNS)
LEVEL_ENTRY_SIZE = 66
PALETTE_BYTES = 16 * 2 * 4


@dataclass(frozen=True)
class LevelEntry:
    start_x: int
    start_y: int
    offset_x: int
    offset_y: int
    floor: int
    chars: int
    map: int
    animation: int
    animation_size: int
    music_id: int
    parallax: int
    palette: int
    block: int
    exit_function: int
    enter_function: int
    block_width: int
    block_height: int
    parallax_function: int
    unused0: int
    unused1: int
    background_swap: int
    padding: int

    @classmethod
    def read(cls, data: bytes, offset: int) -> "LevelEntry":
        if offset < 0 or offset + LEVEL_ENTRY_SIZE > len(data):
            raise ValueError(f"level entry at 0x{offset:X} is outside the ROM")
        values = struct.unpack_from(">HHHHIIIIHHIIIIIHHIII BB", data, offset)
        return cls(*values)

    def as_json(self) -> dict[str, Any]:
        result = asdict(self)
        for key in (
            "floor", "chars", "map", "animation", "parallax", "palette", "block",
            "exit_function", "enter_function", "parallax_function",
        ):
            result[key] = f"0x{result[key]:06X}"
        return result


def _valid_compressed_pointer(data: bytes, address: int, allow_zero: bool) -> bool:
    return (allow_zero and address == 0) or (0 <= address <= len(data) - 18 and is_rnc(data, address))


def valid_level_entry(data: bytes, entry: LevelEntry) -> bool:
    return (
        entry.padding == 0
        and _valid_compressed_pointer(data, entry.floor, False)
        and _valid_compressed_pointer(data, entry.chars, False)
        and _valid_compressed_pointer(data, entry.map, False)
        and _valid_compressed_pointer(data, entry.parallax, True)
    )


def find_level_table(data: bytes) -> int:
    marker = data.find(LEVEL_MARKER)
    if marker < 0:
        raise ValueError("could not find Aladdin's level-table marker")
    return (marker + len(LEVEL_MARKER) + 1) & ~1


def read_level_table(data: bytes, table_offset: int) -> list[tuple[int, LevelEntry, bool]]:
    result = []
    offset = table_offset
    while offset + LEVEL_ENTRY_SIZE <= len(data):
        entry = LevelEntry.read(data, offset)
        valid = valid_level_entry(data, entry)
        if not valid:
            break
        result.append((offset, entry, valid))
        offset += LEVEL_ENTRY_SIZE
    return result


def _write_palette(path: Path, palette_data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = ["JASC-PAL", "0100", "16"]
    for index in range(16):
        word = int.from_bytes(palette_data[index * 2:index * 2 + 2], "big")
        r = ((word >> 1) & 7) * 255 // 7
        g = ((word >> 5) & 7) * 255 // 7
        b = ((word >> 9) & 7) * 255 // 7
        lines.append(f"{r} {g} {b}")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def _read_words(data: bytes, endian: str) -> list[int]:
    if len(data) & 1:
        data = data[:-1]
    return list(struct.unpack(f"{endian}{len(data) // 2}H", data))


def _compose_background(data: bytes, entry: LevelEntry, map_data: bytes) -> list[int]:
    if entry.block_width <= 0 or entry.block_height <= 0:
        raise ValueError("level has no drawable block dimensions")
    map_words = _read_words(map_data, ">")
    needed = entry.block_width * entry.block_height
    if len(map_words) < needed:
        raise ValueError(f"level map has {len(map_words)} words; expected {needed}")
    result: list[int] = []
    for block_y in range(entry.block_height):
        rows = [[], []]
        for block_x in range(entry.block_width):
            block_offset = map_words[block_y * entry.block_width + block_x]
            block_address = entry.block + block_offset
            if block_address < 0 or block_address + 8 > len(data):
                raise ValueError(f"level block pointer 0x{block_address:X} is outside the ROM")
            # The ROM stores each VDP nametable word big-endian, like the
            # 68000. Noesis passes these through a native-endian helper before
            # its renderer; the standalone pipeline keeps the ROM order here.
            tiles = struct.unpack_from(">4H", data, block_address)
            rows[0].extend(tiles[:2])
            rows[1].extend(tiles[2:])
        result.extend(rows[0])
        result.extend(rows[1])
    return result


def _render_parallax(char_data: bytes, palette_data: bytes, parallax_data: bytes, output: Path) -> dict[str, int]:
    words = _read_words(parallax_data, ">")
    width = 64
    height = len(words) // width
    if height <= 0:
        raise ValueError("parallax data has no complete tiles")
    render_tilemap(
        char_data,
        words[:width * height],
        width,
        height,
        palette_data,
        output,
        ppm_output=output.with_suffix(".ppm"),
    )
    return {"width_tiles": width, "height_tiles": height}


def extract_level(data: bytes, index: int, table_offset: int, entry_offset: int, entry: LevelEntry, output_root: Path) -> dict[str, Any]:
    level_dir = output_root / f"level{index:02d}"
    level_dir.mkdir(parents=True, exist_ok=True)
    metadata: dict[str, Any] = {
        "index": index,
        "table_offset": f"0x{table_offset:06X}",
        "entry_offset": f"0x{entry_offset:06X}",
        "entry": entry.as_json(),
        "assets": {},
    }

    palette_data = data[entry.palette:entry.palette + PALETTE_BYTES]
    if len(palette_data) == PALETTE_BYTES:
        palette_path = level_dir / "palettes" / "level.pal"
        _write_palette(palette_path, palette_data)
        (level_dir / "raw").mkdir(exist_ok=True)
        (level_dir / "raw" / "palette.bin").write_bytes(palette_data)
        metadata["assets"]["palette"] = {"rom": f"0x{entry.palette:06X}", "bytes": len(palette_data)}

    for name, address in (("floor", entry.floor), ("chars", entry.chars), ("map", entry.map)):
        block = decompress_at(data, address)
        raw_path = level_dir / "raw" / f"{name}.bin"
        raw_path.parent.mkdir(exist_ok=True)
        raw_path.write_bytes(block.data)
        metadata["assets"][name] = {
            "rom": f"0x{address:06X}",
            "compressed_bytes": block.header.total_size,
            "decompressed_bytes": len(block.data),
            "method": block.header.method,
        }

    if entry.parallax:
        block = decompress_at(data, entry.parallax)
        (level_dir / "raw" / "parallax.bin").write_bytes(block.data)
        metadata["assets"]["parallax"] = {
            "rom": f"0x{entry.parallax:06X}",
            "compressed_bytes": block.header.total_size,
            "decompressed_bytes": len(block.data),
            "method": block.header.method,
        }
    if entry.animation and entry.animation_size:
        animation = data[entry.animation:entry.animation + entry.animation_size]
        (level_dir / "raw" / "animation.bin").write_bytes(animation)
        metadata["assets"]["animation"] = {
            "rom": f"0x{entry.animation:06X}",
            "bytes": len(animation),
        }

    chars_data = (level_dir / "raw" / "chars.bin").read_bytes()
    map_data = (level_dir / "raw" / "map.bin").read_bytes()
    # A normal Aladdin room is often wider than a Genesis viewport.  Keep the
    # limit high enough for the full level backgrounds while still refusing
    # the special picture-room entry whose table advertises 15000 blocks.
    if entry.block_width * entry.block_height <= 65536:
        words = _compose_background(data, entry, map_data)
        width = entry.block_width * 2
        height = entry.block_height * 2
        render_tilemap(
            chars_data,
            words,
            width,
            height,
            palette_data,
            level_dir / "background.png",
            ppm_output=level_dir / "background.ppm",
        )
        metadata["rendered"] = {"background": {"width_tiles": width, "height_tiles": height}}
        if entry.parallax:
            try:
                parallax_data = (level_dir / "raw" / "parallax.bin").read_bytes()
                metadata["rendered"]["parallax"] = _render_parallax(chars_data, palette_data, parallax_data, level_dir / "parallax.png")
            except (ValueError, RncError) as error:
                metadata.setdefault("warnings", []).append(f"parallax: {error}")
    else:
        metadata.setdefault("warnings", []).append("background dimensions exceed safe renderer limit")
    return metadata


def extract_levels(data: bytes, output_root: Path) -> dict[str, Any]:
    table_offset = find_level_table(data)
    table = read_level_table(data, table_offset)
    if not table:
        raise ValueError(f"level table at 0x{table_offset:X} contains no valid entries")
    levels = []
    for index, (entry_offset, entry, _) in enumerate(table):
        try:
            levels.append(extract_level(data, index, table_offset, entry_offset, entry, output_root))
        except (RncError, ValueError, IndexError, struct.error) as error:
            level_dir = output_root / f"level{index:02d}"
            level_dir.mkdir(parents=True, exist_ok=True)
            levels.append({
                "index": index,
                "table_offset": f"0x{table_offset:06X}",
                "entry_offset": f"0x{entry_offset:06X}",
                "entry": entry.as_json(),
                "error": str(error),
            })
    return {"table_offset": f"0x{table_offset:06X}", "count": len(levels), "levels": levels}
