#!/usr/bin/env python3
"""Verify native plane-A/plane-B composition against the extracted assets."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from genie.analysis.visual_diff import read_image


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "build/assets/levels/level01"
BINARY = ROOT / "build/openaladdin"
LEVELS = (0, 52, 87, 116, 144, 172, 206, 255)
BACKGROUND_ORIGIN = (32, 480)
PARALLAX_SOURCE_X = 0x79


def palette_color(word: int) -> tuple[int, int, int]:
    return tuple(LEVELS[(word >> shift) & 7] for shift in (1, 5, 9))


def compose_expected() -> tuple[int, int, bytes]:
    background = read_image(ASSETS / "background.ppm")
    parallax = read_image(ASSETS / "parallax.ppm")
    palette_data = (ASSETS / "raw/palette.bin").read_bytes()
    palette = [
        palette_color(int.from_bytes(palette_data[index:index + 2], "big"))
        for index in range(0, len(palette_data), 2)
    ]
    transparent = {palette[line * 16] for line in range(4)}
    width, height = 320, 224
    output = bytearray(width * height * 3)
    backdrop = bytes(palette[0])
    for y in range(height):
        for x in range(width):
            parallax_x = (PARALLAX_SOURCE_X + x) % parallax[0]
            parallax_offset = ((y * parallax[0] + parallax_x) * 3)
            color = bytes(parallax[2][parallax_offset:parallax_offset + 3])
            if tuple(color) in transparent:
                color = backdrop

            background_x = BACKGROUND_ORIGIN[0] + x
            background_y = BACKGROUND_ORIGIN[1] + y
            background_offset = ((background_y * background[0] + background_x) * 3)
            background_color = bytes(
                background[2][background_offset:background_offset + 3]
            )
            if tuple(background_color) not in transparent:
                color = background_color
            output[(y * width + x) * 3:(y * width + x + 1) * 3] = color
    return width, height, bytes(output)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="openaladdin-visual-layers-") as directory:
        output = Path(directory) / "frame.ppm"
        subprocess.run(
            [
                str(BINARY),
                "--no-window",
                # This test isolates the two extracted planes. VDP SAT/HUD
                # coverage has its own renderer regression; omitting the ROM
                # keeps those sprites out of this plane-only fixture.
                "--rom",
                "",
                "--render-checkpoint",
                "--checkpoint-player",
                "10000,416,0,0,1",
                "--checkpoint-frame-ptr",
                "0x1ec114",
                "--checkpoint-animation",
                "0x121e46,0",
                "--checkpoint-camera",
                "16,464,16,464,0,0,1",
                "--framebuffer-out",
                str(output),
            ],
            cwd=ROOT,
            check=True,
        )
        actual = read_image(output)
    expected = compose_expected()
    assert actual == expected, "native VDP plane composition differs from extracted layers"
    print("native visual layers: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
