"""Rob Northen Compression (RNC/ProPack method 1) decompression.

The Aladdin level data uses RNC method 1.  The bitstream is little-bit-endian
inside a big-endian 68K ROM and contains three small canonical Huffman tables
per pack block.  This module deliberately exposes the header and consumed
length so callers can preserve exact ROM provenance in their manifests.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct


class RncError(ValueError):
    """Raised when an RNC block is malformed or fails its checksums."""


def _make_crc16_table() -> tuple[int, ...]:
    table = []
    for value in range(256):
        crc = value
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
        table.append(crc)
    return tuple(table)


_CRC16_TABLE = _make_crc16_table()


def crc16(data: bytes) -> int:
    value = 0
    for byte in data:
        value = (value >> 8) ^ _CRC16_TABLE[(value ^ byte) & 0xFF]
    return value


@dataclass(frozen=True)
class RncHeader:
    method: int
    unpacked_size: int
    packed_size: int
    unpacked_crc: int
    packed_crc: int
    leeway: int
    chunks: int

    @property
    def total_size(self) -> int:
        return 18 + self.packed_size


@dataclass(frozen=True)
class RncBlock:
    offset: int
    header: RncHeader
    data: bytes


@dataclass(frozen=True)
class _Leaf:
    code: int
    bits: int
    value: int


class _BitStream:
    """RNC's LSB-first bit reader with byte-stream resynchronization."""

    def __init__(self, data: bytes, offset: int):
        self.data = data
        self.offset = offset
        self.bits = 0
        self.available = 0

    def read_byte(self) -> int:
        if self.offset >= len(self.data):
            raise RncError("RNC bitstream read past input")
        value = self.data[self.offset]
        self.offset += 1
        return value

    def read_bits(self, count: int) -> int:
        if count < 0 or count > 16:
            raise RncError(f"invalid RNC bit count: {count}")
        result = 0
        output_bit = 1
        for _ in range(count):
            if self.available == 0:
                b1 = self.read_byte()
                b2 = self.read_byte()
                b3 = self.data[self.offset] if self.offset < len(self.data) else 0
                b4 = self.data[self.offset + 1] if self.offset + 1 < len(self.data) else 0
                self.bits = (b4 << 24) | (b3 << 16) | (b2 << 8) | b1
                self.available = 16
            if self.bits & 1:
                result |= output_bit
            self.bits >>= 1
            output_bit <<= 1
            self.available -= 1
        return result

    def resync(self) -> None:
        old_bits = self.bits
        b8 = self.data[self.offset] if self.offset < len(self.data) else 0
        b16 = self.data[self.offset + 1] if self.offset + 1 < len(self.data) else 0
        b24 = self.data[self.offset + 2] if self.offset + 2 < len(self.data) else 0
        self.bits = (((b24 << 16) | (b16 << 8) | b8) << self.available)
        self.bits |= old_bits & ((1 << self.available) - 1)
        self.bits &= 0xFFFFFFFF


def _inverse_bits(value: int, count: int) -> int:
    result = 0
    for _ in range(count):
        result = (result << 1) | (value & 1)
        value >>= 1
    return result


def _build_table(lengths: list[int]) -> list[_Leaf]:
    leaves: list[_Leaf] = []
    value = 0
    divisor = 0x80000000
    for bit_count in range(1, 17):
        for index, length in enumerate(lengths):
            if length == bit_count:
                leaves.append(_Leaf(_inverse_bits(value // divisor, bit_count), bit_count, index))
                value += divisor
        divisor >>= 1
    return leaves


def _read_table(bits: _BitStream) -> list[_Leaf]:
    count = bits.read_bits(5)
    if count == 0 or count > 16:
        raise RncError(f"invalid RNC Huffman leaf count: {count}")
    return _build_table([bits.read_bits(4) for _ in range(count)])


def _read_huffman(table: list[_Leaf], bits: _BitStream) -> int:
    for leaf in table:
        if (bits.bits & ((1 << leaf.bits) - 1)) == leaf.code:
            bits.read_bits(leaf.bits)
            if leaf.value < 2:
                return leaf.value
            return bits.read_bits(leaf.value - 1) | (1 << (leaf.value - 1))
    raise RncError(f"no RNC Huffman code matches 0x{bits.bits:08X}")


def parse_header(data: bytes, offset: int = 0) -> RncHeader:
    if offset < 0 or offset + 18 > len(data):
        raise RncError("RNC header is outside the input")
    magic, unpacked, packed, unpacked_crc, packed_crc, leeway = struct.unpack_from(">IIIHHH", data, offset)
    if magic != 0x524E4301:
        raise RncError(f"not an RNC method 1 block at 0x{offset:X}")
    return RncHeader(1, unpacked, packed, unpacked_crc, packed_crc, leeway >> 8, leeway & 0xFF)


def decompress_at(data: bytes, offset: int = 0, verify: bool = True) -> RncBlock:
    header = parse_header(data, offset)
    end = offset + header.total_size
    if end > len(data):
        raise RncError(f"RNC block at 0x{offset:X} is truncated")
    packed = data[offset + 18:end]
    if verify and crc16(packed) != header.packed_crc:
        raise RncError(f"RNC packed CRC mismatch at 0x{offset:X}")

    bits = _BitStream(data, offset + 18)
    bits.read_bits(2)
    output = bytearray()

    while bits.offset < end and len(output) < header.unpacked_size:
        raw_table = _read_table(bits)
        length_table = _read_table(bits)
        position_table = _read_table(bits)
        chunk_count = bits.read_bits(16)
        if chunk_count == 0:
            raise RncError(f"empty RNC chunk block at 0x{offset:X}")

        while chunk_count:
            raw_length = _read_huffman(raw_table, bits)
            if raw_length:
                output.extend(bits.read_byte() for _ in range(raw_length))
            bits.resync()
            chunk_count -= 1
            if chunk_count:
                distance = _read_huffman(length_table, bits) + 1
                length = _read_huffman(position_table, bits) + 2
                if distance > len(output):
                    raise RncError(f"RNC back-reference before output at 0x{offset:X}")
                for _ in range(length):
                    output.append(output[-distance])

    if len(output) != header.unpacked_size:
        raise RncError(
            f"RNC size mismatch at 0x{offset:X}: expected {header.unpacked_size}, got {len(output)}"
        )
    result = bytes(output)
    if verify and crc16(result) != header.unpacked_crc:
        raise RncError(f"RNC unpacked CRC mismatch at 0x{offset:X}")
    return RncBlock(offset, header, result)


def is_rnc(data: bytes, offset: int = 0) -> bool:
    return offset >= 0 and offset + 4 <= len(data) and data[offset:offset + 4] == b"RNC\x01"
