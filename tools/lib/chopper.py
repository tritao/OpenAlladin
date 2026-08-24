"""Virgin/Chopper animated sprite extraction for Aladdin's Genesis ROM."""

from __future__ import annotations

from dataclasses import dataclass, asdict
import json
from pathlib import Path
import struct
from typing import Any

from .genesis import decode_tile, genesis_color
from .png import write_rgba


BANK_OFFSETS = (0x9500, 0x9600, 0x9700)
REQUIRED_MATCH_COUNT = 8
MAX_VALID_PART_COUNT = 128
MAX_TILE_SIZE = 16

# The same default palette documented by the Noesis Chopper script.  Runtime
# palette association is game-state dependent, so it is kept explicit in the
# manifest rather than pretending this is correct for every animation.
DEFAULT_PALETTE_WORDS = (
    0x707, 0x763, 0x751, 0x642, 0x531, 0x420, 0x310, 0x200,
    0x401, 0x347, 0x236, 0x202, 0x444, 0x222, 0x777, 0x000,
)


def _u8(data: bytes, offset: int) -> int:
    return data[offset]


def _u16(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 2], "big")


def _u32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 4], "big")


def _signed8(value: int) -> int:
    return value - 256 if value & 0x80 else value


@dataclass
class ChopperTileInfo:
    address: int
    memory_size: int
    tile_size: int
    pixel_width: int
    pixel_height: int

    @property
    def tile_width(self) -> int:
        return self.pixel_width >> 3

    @property
    def tile_height(self) -> int:
        return self.pixel_height >> 3

    @property
    def key(self) -> str:
        return f"{self.tile_width}x{self.tile_height}"


@dataclass
class ChopperPart:
    info_address: int
    offset_raw: tuple[int, int]
    tile_address: int
    tile_size: int
    info: ChopperTileInfo
    tile_index: int | None = None

    def as_json(self) -> dict[str, Any]:
        return {
            "info_address": f"0x{self.info_address:06X}",
            "offset": list(self.offset_raw),
            "offset_signed": [_signed8(v) for v in self.offset_raw],
            "tile_address": f"0x{self.tile_address:06X}",
            "tile_size": self.tile_size,
            "tile_set": self.info.key,
            "tile_index": self.tile_index,
            "tile_info": {
                "address": f"0x{self.info.address:06X}",
                "memory_size": self.info.memory_size,
                "tile_size": self.info.tile_size,
                "pixel_width": self.info.pixel_width,
                "pixel_height": self.info.pixel_height,
            },
        }


@dataclass
class ChopperFrame:
    address: int
    struct_size: int
    collision_min: tuple[int, int]
    collision_max: tuple[int, int]
    parts: list[ChopperPart]

    def as_json(self) -> dict[str, Any]:
        return {
            "address": f"0x{self.address:06X}",
            "struct_size": self.struct_size,
            "collision_min": list(self.collision_min),
            "collision_max": list(self.collision_max),
            "parts": [part.as_json() for part in self.parts],
        }


class ChopperParser:
    def __init__(self, rom: bytes):
        self.rom = rom

    def read_frame(self, address: int) -> ChopperFrame | None:
        if address < 0 or address + 6 > len(self.rom):
            return None
        start = address
        part_count = _u16(self.rom, address) + 1
        address += 2
        if part_count > MAX_VALID_PART_COUNT or address + 4 > len(self.rom):
            return None
        collision_min = (_u8(self.rom, address), _u8(self.rom, address + 1))
        collision_max = (_u8(self.rom, address + 2), _u8(self.rom, address + 3))
        address += 4
        parts: list[ChopperPart] = []
        for _ in range(part_count):
            if address + 12 > len(self.rom):
                return None
            info_address = _u16(self.rom, address)
            offset_raw = (_u8(self.rom, address + 2), _u8(self.rom, address + 3))
            encoded = (_u16(self.rom, address + 4), _u16(self.rom, address + 6), _u16(self.rom, address + 8))
            tile_address = (
                ((encoded[0] - BANK_OFFSETS[0]) << 1)
                | ((encoded[1] - BANK_OFFSETS[1]) << 9)
                | ((encoded[2] - BANK_OFFSETS[2]) << 17)
            )
            tile_size = _u16(self.rom, address + 10)
            if info_address >= len(self.rom) or tile_address < 0 or tile_address >= len(self.rom):
                return None
            parts.append(ChopperPart(info_address, offset_raw, tile_address, tile_size, ChopperTileInfo(0, 0, 0, 0, 0)))
            address += 12

        end = address
        for part in parts:
            if part.info_address + 10 > len(self.rom):
                return None
            info = ChopperTileInfo(
                _u32(self.rom, part.info_address),
                _u16(self.rom, part.info_address + 4),
                _u16(self.rom, part.info_address + 6),
                _u8(self.rom, part.info_address + 8),
                _u8(self.rom, part.info_address + 9),
            )
            if not (0 < info.tile_width <= MAX_TILE_SIZE and 0 < info.tile_height <= MAX_TILE_SIZE):
                return None
            if part.tile_address + info.tile_width * info.tile_height * 32 > len(self.rom):
                return None
            part.info = info
        return ChopperFrame(start, end - start, collision_min, collision_max, parts)

    def find_pointer_table(self) -> tuple[int, int] | None:
        offset = 0x200
        end_offset = min(0x1000, len(self.rom) - 4)
        valid_start = -1
        expected_size = -1
        last_pointer = -1
        while offset < end_offset:
            pointer = _u32(self.rom, offset)
            if valid_start >= 0 and pointer - last_pointer != expected_size:
                valid_start = -1
            frame = self.read_frame(pointer) if pointer > offset and pointer < len(self.rom) - 16 else None
            if frame is not None:
                expected_size = frame.struct_size
                last_pointer = pointer
                if valid_start < 0:
                    valid_start = offset
            else:
                valid_start = -1
            offset += 4 if valid_start >= 0 else 2
            if valid_start >= 0 and offset - valid_start >= REQUIRED_MATCH_COUNT * 4:
                return valid_start, expected_size
        return None

    def read_frames(self, pointer_table: int) -> list[tuple[int, ChopperFrame]]:
        frames = []
        offset = pointer_table
        while offset + 4 <= len(self.rom):
            pointer = _u32(self.rom, offset)
            if pointer >= len(self.rom) - 16:
                break
            frame = self.read_frame(pointer)
            if frame is None:
                break
            frames.append((pointer, frame))
            offset += 4
        return frames


class _TileSet:
    def __init__(self, width: int, height: int):
        self.width = width
        self.height = height
        self.tile_bytes = width * height * 32
        self.data = bytearray()
        self.addresses: dict[int, int] = {}

    def index_for(self, rom: bytes, address: int) -> int:
        if address in self.addresses:
            return self.addresses[address]
        index = len(self.data) // self.tile_bytes
        self.addresses[address] = index
        self.data.extend(rom[address:address + self.tile_bytes])
        return index


def _default_palette() -> list[tuple[int, int, int, int]]:
    return [genesis_color(value) for value in DEFAULT_PALETTE_WORDS]


def _render_frame(frame: ChopperFrame, tile_sets: dict[str, _TileSet], output: Path) -> None:
    bounds = []
    for part in frame.parts:
        x, y = (_signed8(part.offset_raw[0]), _signed8(part.offset_raw[1]))
        bounds.append((x, y, x + part.info.pixel_width, y + part.info.pixel_height))
    if not bounds:
        return
    min_x = min(item[0] for item in bounds)
    min_y = min(item[1] for item in bounds)
    max_x = max(item[2] for item in bounds)
    max_y = max(item[3] for item in bounds)
    width = max(1, max_x - min_x)
    height = max(1, max_y - min_y)
    pixels = bytearray(width * height * 4)
    palette = _default_palette()
    for part in frame.parts:
        tile_set = tile_sets[part.info.key]
        assert part.tile_index is not None
        base = part.tile_index * tile_set.tile_bytes
        x0 = _signed8(part.offset_raw[0]) - min_x
        y0 = _signed8(part.offset_raw[1]) - min_y
        for ty in range(part.info.tile_height):
            for tx in range(part.info.tile_width):
                tile_offset = base + (ty * part.info.tile_width + tx) * 32
                tile = decode_tile(bytes(tile_set.data[tile_offset:tile_offset + 32]), 0)
                for y in range(8):
                    for x in range(8):
                        color_index = tile[y][x]
                        if color_index == 0:
                            continue
                        dx = x0 + tx * 8 + x
                        dy = y0 + ty * 8 + y
                        if 0 <= dx < width and 0 <= dy < height:
                            dest = (dy * width + dx) * 4
                            pixels[dest:dest + 4] = bytes(palette[color_index])
    write_rgba(output, width, height, pixels)


def extract_chopper(rom: bytes, output_root: Path) -> dict[str, Any]:
    parser = ChopperParser(rom)
    if rom[0x100:0x104] != b"SEGA":
        return {"supported": False, "reason": "missing Genesis header"}
    found = parser.find_pointer_table()
    if found is None:
        return {"supported": False, "reason": "no Chopper pointer table found"}
    pointer_table, struct_size = found
    frames = parser.read_frames(pointer_table)
    if not frames:
        return {"supported": False, "reason": "Chopper pointer table contained no frames"}

    output_root.mkdir(parents=True, exist_ok=True)
    tile_sets: dict[str, _TileSet] = {}
    frame_json = []
    for index, (_, frame) in enumerate(frames):
        for part in frame.parts:
            tile_set = tile_sets.setdefault(part.info.key, _TileSet(part.info.tile_width, part.info.tile_height))
            part.tile_index = tile_set.index_for(rom, part.tile_address)
        frame_path = output_root / "frames" / f"frame{index:04d}.png"
        _render_frame(frame, tile_sets, frame_path)
        frame_json.append({"index": index, **frame.as_json()})

    sets_json = {}
    for key, tile_set in tile_sets.items():
        file_name = f"SPR_{tile_set.width}X{tile_set.height}A.SEG"
        path = output_root / "tiles" / file_name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(tile_set.data)
        sets_json[key] = {
            "width_tiles": tile_set.width,
            "height_tiles": tile_set.height,
            "tile_bytes": tile_set.tile_bytes,
            "count": len(tile_set.data) // tile_set.tile_bytes,
            "file": str(path.relative_to(output_root.parent)),
        }

    result = {
        "supported": True,
        "pointer_table": f"0x{pointer_table:06X}",
        "frame_struct_size": struct_size,
        "frame_count": len(frames),
        "tile_sets": sets_json,
        "default_palette_words": [f"0x{word:03X}" for word in DEFAULT_PALETTE_WORDS],
        "frames": frame_json,
    }
    (output_root / "frames.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result
