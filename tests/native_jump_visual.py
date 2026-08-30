#!/usr/bin/env python3
"""Check the native player sprite placement across a MAME jump arc.

The state regressions already compare the physics fields.  This fixture keeps
the corresponding MAME player origins at several ascent, apex, and landing
boundaries, then verifies that the native framebuffer puts the decoded
multipart frame at those same screen coordinates.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from genie.analysis.visual_diff import read_alpha, read_image


BINARY = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/openaladdin"
FRAME_MANIFEST = ROOT / "build/assets/sprites/frames.json"

# These are synchronized player checkpoints from the ROM's first-floor jump
# experiment.  The expected screen origin is the MAME SAT origin: the player
# local Y is measured from the terrain row, while the sprite origin is one
# 0x100-pixel tile above it.  Keeping the observed origins here is what makes
# this a placement regression rather than a second copy of native physics.
CASES = (
    # name, player x, player y, camera x, camera y, grounded, frame pointer
    ("early-ascent", 148, 414, 45, 464, 0, 0x001E94CE),
    ("mid-ascent", 148, 408, 45, 464, 0, 0x001E951C),
    ("near-apex", 151, 394, 42, 473, 0, 0x001E956A),
    ("apex", 153, 389, 40, 478, 0, 0x001E95D0),
    ("falling", 157, 391, 36, 478, 0, 0x001E964E),
    ("landed", 161, 402, 32, 478, 1, 0x001E986A),
)


def frame_record(pointer: int) -> dict[str, object]:
    document = json.loads(FRAME_MANIFEST.read_text(encoding="utf-8"))
    for record in document["frames"]:
        address = record["address"]
        address = int(address, 0) if isinstance(address, str) else int(address)
        if address == pointer:
            return record
    raise AssertionError(f"sprite frame {pointer:#x} is not in the manifest")


def expected_pixels(
    record: dict[str, object],
    player_x: int,
    player_y: int,
) -> set[tuple[int, int]]:
    frame_index = int(record["index"])
    frame_path = FRAME_MANIFEST.parent / "frames" / f"frame{frame_index:04d}.png"
    width, height, alpha = read_alpha(frame_path)
    parts = record["parts"]
    offsets = [
        part.get("offset_pixels") or part["offset_signed"]
        for part in parts
    ]
    min_x = min(int(offset[0]) for offset in offsets)
    min_y = min(int(offset[1]) for offset in offsets)
    # MAME's sprite origin is one 0x100-pixel tile above the player terrain
    # coordinate.  The camera cancels because player_x/player_y are local.
    canvas_x = player_x + min_x
    canvas_y = player_y - 0x100 + min_y
    return {
        (canvas_x + x, canvas_y + y)
        for y in range(height)
        for x in range(width)
        if alpha[y * width + x] != 0
    }


def render_checkpoint(
    output: Path,
    *,
    player_x: int,
    player_y: int,
    camera_x: int,
    camera_y: int,
    grounded: int,
    frame_pointer: int,
) -> None:
    command = [
        str(BINARY),
        "--no-window",
        "--no-audio",
        "--rom",
        "",
        "--render-checkpoint",
        "--checkpoint-player",
        f"{player_x},{player_y},0,0,{grounded}",
        "--checkpoint-frame-ptr",
        hex(frame_pointer),
        "--checkpoint-camera",
        f"{camera_x},{camera_y},{camera_x},{camera_y},0,0,1",
        "--framebuffer-out",
        str(output),
    ]
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    result = subprocess.run(
        command,
        cwd=ROOT,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr


def changed_pixels(baseline: tuple[int, int, bytes], actual: tuple[int, int, bytes]) -> set[tuple[int, int]]:
    width, height, baseline_pixels = baseline
    actual_width, actual_height, actual_pixels = actual
    assert (width, height) == (actual_width, actual_height)
    return {
        (index % width, index // width)
        for index in range(width * height)
        if baseline_pixels[index * 3:index * 3 + 3]
        != actual_pixels[index * 3:index * 3 + 3]
    }


def main() -> int:
    assert BINARY.is_file(), f"native binary not found: {BINARY}"
    assert FRAME_MANIFEST.is_file(), f"sprite manifest not found: {FRAME_MANIFEST}"

    with tempfile.TemporaryDirectory(prefix="openaladdin-jump-visual-") as directory:
        root = Path(directory)
        for name, player_x, player_y, camera_x, camera_y, grounded, pointer in CASES:
            baseline_path = root / f"{name}-baseline.ppm"
            actual_path = root / f"{name}.ppm"
            # Move the player off-screen for a same-camera baseline.  The
            # changed-pixel set then isolates the player without assuming
            # anything about the preview background artwork.
            render_checkpoint(
                baseline_path,
                player_x=10000,
                player_y=player_y,
                camera_x=camera_x,
                camera_y=camera_y,
                grounded=grounded,
                frame_pointer=pointer,
            )
            render_checkpoint(
                actual_path,
                player_x=player_x,
                player_y=player_y,
                camera_x=camera_x,
                camera_y=camera_y,
                grounded=grounded,
                frame_pointer=pointer,
            )
            actual_changed = changed_pixels(
                read_image(baseline_path),
                read_image(actual_path),
            )
            expected = expected_pixels(frame_record(pointer), player_x, player_y)
            assert actual_changed <= expected, (
                f"{name}: native rendered pixels outside the MAME frame mask; "
                f"extra={len(actual_changed - expected)}"
            )
            actual_bbox = (
                min(x for x, _ in actual_changed),
                min(y for _, y in actual_changed),
                max(x for x, _ in actual_changed),
                max(y for _, y in actual_changed),
            )
            expected_bbox = (
                min(x for x, _ in expected),
                min(y for _, y in expected),
                max(x for x, _ in expected),
                max(y for _, y in expected),
            )
            assert actual_bbox == expected_bbox, (
                f"{name}: native sprite bbox {actual_bbox} != "
                f"MAME bbox {expected_bbox}"
            )

    print("native jump visual: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
