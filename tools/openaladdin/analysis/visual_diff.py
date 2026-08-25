#!/usr/bin/env python3
"""Compare native and MAME framebuffer captures without third-party packages."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys
import zlib


RGBImage = tuple[int, int, bytes]


def _ppm(path: Path, data: bytes) -> RGBImage:
    if not data.startswith(b"P6"):
        raise ValueError(f"{path}: expected binary P6 PPM")

    position = 0

    def token() -> bytes:
        nonlocal position
        while position < len(data):
            if data[position] == ord("#"):
                end = data.find(b"\n", position)
                position = len(data) if end < 0 else end + 1
            elif data[position] in b" \t\r\n":
                position += 1
            else:
                break
        start = position
        while position < len(data) and data[position] not in b" \t\r\n#":
            position += 1
        if start == position:
            raise ValueError(f"{path}: malformed PPM header")
        return data[start:position]

    magic = token()
    if magic != b"P6":
        raise ValueError(f"{path}: unsupported PPM format {magic!r}")
    width = int(token())
    height = int(token())
    maximum = int(token())
    if width <= 0 or height <= 0 or maximum != 255:
        raise ValueError(f"{path}: expected positive 8-bit PPM dimensions")
    if data[position:position + 2] == b"\r\n":
        position += 2
    elif position < len(data) and data[position] in b" \t\r\n":
        position += 1
    pixels = data[position:]
    expected = width * height * 3
    if len(pixels) != expected:
        raise ValueError(f"{path}: expected {expected} RGB bytes, found {len(pixels)}")
    return width, height, pixels


def _paeth(left: int, up: int, upper_left: int) -> int:
    estimate = left + up - upper_left
    left_distance = abs(estimate - left)
    up_distance = abs(estimate - up)
    upper_left_distance = abs(estimate - upper_left)
    if left_distance <= up_distance and left_distance <= upper_left_distance:
        return left
    if up_distance <= upper_left_distance:
        return up
    return upper_left


def _png(path: Path, data: bytes) -> RGBImage:
    signature = b"\x89PNG\r\n\x1a\n"
    if not data.startswith(signature):
        raise ValueError(f"{path}: expected PNG or P6 PPM")
    position = len(signature)
    width = height = bit_depth = color_type = None
    compressed = bytearray()
    while position + 12 <= len(data):
        length = struct.unpack_from(">I", data, position)[0]
        kind = data[position + 4:position + 8]
        payload_start = position + 8
        payload_end = payload_start + length
        if payload_end + 4 > len(data):
            raise ValueError(f"{path}: truncated PNG chunk")
        payload = data[payload_start:payload_end]
        if kind == b"IHDR":
            if len(payload) != 13:
                raise ValueError(f"{path}: malformed PNG IHDR")
            width, height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if compression != 0 or filtering != 0 or interlace != 0:
                raise ValueError(f"{path}: unsupported PNG compression/filter/interlace")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
        position = payload_end + 4

    if width is None or height is None or bit_depth != 8:
        raise ValueError(f"{path}: expected an 8-bit non-interlaced PNG")
    if color_type not in (2, 6):
        raise ValueError(f"{path}: expected RGB or RGBA PNG, color type {color_type}")
    channels = 3 if color_type == 2 else 4
    stride = width * channels
    decoded = zlib.decompress(bytes(compressed))
    expected = height * (stride + 1)
    if len(decoded) != expected:
        raise ValueError(f"{path}: decompressed PNG size mismatch")

    rows: list[bytes] = []
    previous = bytearray(stride)
    position = 0
    for _ in range(height):
        filter_type = decoded[position]
        position += 1
        source = decoded[position:position + stride]
        position += stride
        row = bytearray(stride)
        for index, value in enumerate(source):
            left = row[index - channels] if index >= channels else 0
            up = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_type == 0:
                result = value
            elif filter_type == 1:
                result = value + left
            elif filter_type == 2:
                result = value + up
            elif filter_type == 3:
                result = value + ((left + up) // 2)
            elif filter_type == 4:
                result = value + _paeth(left, up, upper_left)
            else:
                raise ValueError(f"{path}: unsupported PNG filter {filter_type}")
            row[index] = result & 0xFF
        rows.append(bytes(row))
        previous = row

    if color_type == 2:
        pixels = b"".join(rows)
    else:
        rgb = bytearray(width * height * 3)
        destination = 0
        for row in rows:
            for index in range(0, len(row), 4):
                rgb[destination:destination + 3] = row[index:index + 3]
                destination += 3
        pixels = bytes(rgb)
    return width, height, pixels


def read_image(path: Path) -> RGBImage:
    data = path.read_bytes()
    if data.startswith(b"P6"):
        return _ppm(path, data)
    return _png(path, data)


def parse_region(value: str | None, width: int, height: int) -> tuple[int, int, int, int]:
    if value is None:
        return 0, 0, width, height
    fields = [int(item) for item in value.split(",")]
    if len(fields) != 4:
        raise ValueError("--region expects x,y,width,height")
    x, y, region_width, region_height = fields
    if x < 0 or y < 0 or region_width <= 0 or region_height <= 0:
        raise ValueError("--region must be positive and non-negative")
    if x + region_width > width or y + region_height > height:
        raise ValueError("--region extends beyond the image")
    return x, y, region_width, region_height


def write_ppm(path: Path, width: int, height: int, pixels: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + pixels)


def compare(expected: RGBImage, actual: RGBImage, region: tuple[int, int, int, int]) -> dict[str, object]:
    expected_width, expected_height, expected_pixels = expected
    actual_width, actual_height, actual_pixels = actual
    if (expected_width, expected_height) != (actual_width, actual_height):
        raise ValueError(
            "image dimensions differ: "
            f"expected {expected_width}x{expected_height}, actual {actual_width}x{actual_height}"
        )

    x0, y0, width, height = region
    difference_count = 0
    channel_delta_sum = 0
    max_channel_delta = 0
    bounding_box: list[int] | None = None
    overlay = bytearray(actual_pixels)
    for y in range(y0, y0 + height):
        for x in range(x0, x0 + width):
            offset = (y * expected_width + x) * 3
            expected_pixel = expected_pixels[offset:offset + 3]
            actual_pixel = actual_pixels[offset:offset + 3]
            deltas = [abs(left - right) for left, right in zip(expected_pixel, actual_pixel)]
            if not any(deltas):
                continue
            difference_count += 1
            channel_delta_sum += sum(deltas)
            max_channel_delta = max(max_channel_delta, *deltas)
            if bounding_box is None:
                bounding_box = [x, y, x, y]
            else:
                bounding_box[0] = min(bounding_box[0], x)
                bounding_box[1] = min(bounding_box[1], y)
                bounding_box[2] = max(bounding_box[2], x)
                bounding_box[3] = max(bounding_box[3], y)
            overlay[offset:offset + 3] = b"\xff\x00\xff"

    compared_pixels = width * height
    return {
        "format": "openaladdin-visual-diff-v1",
        "width": expected_width,
        "height": expected_height,
        "region": [x0, y0, width, height],
        "compared_pixels": compared_pixels,
        "different_pixels": difference_count,
        "difference_ratio": difference_count / compared_pixels,
        "channel_delta_sum": channel_delta_sum,
        "mean_channel_delta": channel_delta_sum / (compared_pixels * 3),
        "max_channel_delta": max_channel_delta,
        "bounding_box": bounding_box,
        "overlay_pixels": bytes(overlay),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("expected", type=Path, help="MAME PNG or reference PPM")
    parser.add_argument("actual", type=Path, help="native framebuffer PPM or PNG")
    parser.add_argument("--region", help="only compare x,y,width,height")
    parser.add_argument("--overlay", type=Path, help="write a PPM with differences highlighted magenta")
    parser.add_argument("--json", type=Path, help="write the metrics as JSON")
    parser.add_argument("--max-different-pixels", type=int, default=0)
    parser.add_argument("--max-channel-error", type=int, default=0)
    args = parser.parse_args()

    try:
        expected = read_image(args.expected)
        actual = read_image(args.actual)
        region = parse_region(args.region, expected[0], expected[1])
        report = compare(expected, actual, region)
    except (OSError, ValueError, zlib.error) as error:
        print(f"visual diff: {error}", file=sys.stderr)
        return 2

    overlay_pixels = report.pop("overlay_pixels")
    if args.overlay:
        write_ppm(args.overlay, expected[0], expected[1], overlay_pixels)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(
        "visual diff: "
        f"{report['width']}x{report['height']} "
        f"region={report['region']} "
        f"different={report['different_pixels']}/{report['compared_pixels']} "
        f"max_channel_delta={report['max_channel_delta']} "
        f"bbox={report['bounding_box']}"
    )
    failed = (
        report["different_pixels"] > args.max_different_pixels
        or report["max_channel_delta"] > args.max_channel_error
    )
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
