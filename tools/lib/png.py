"""Small dependency-free RGBA PNG writer."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path


def write_rgba(path: Path, width: int, height: int, pixels: bytes | bytearray) -> None:
    expected = width * height * 4
    if len(pixels) != expected:
        raise ValueError(f"RGBA buffer has {len(pixels)} bytes; expected {expected}")

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    rows = bytearray()
    stride = width * 4
    for row in range(height):
        rows.append(0)
        start = row * stride
        rows.extend(pixels[start:start + stride])
    payload = b"\x89PNG\r\n\x1a\n"
    payload += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    payload += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    payload += chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
