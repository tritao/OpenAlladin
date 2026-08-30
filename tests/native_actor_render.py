#!/usr/bin/env python3
"""Regression for rendering a live native actor into the framebuffer."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
ACTORS = ROOT / "re/actors/guard-collision.tsv"


def render(actor_records: Path | None, output: Path) -> bytes:
    command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--no-audio",
        "--frames",
        "1",
        "--framebuffer-out",
        str(output),
        "--framebuffer-frame",
        "1",
        "--checkpoint-player",
        "103,368,0,0,1",
        "--checkpoint-camera",
        "1280,464,1280,464,0,0,1",
        "--input-schedule",
        "none*1",
    ]
    if actor_records is not None:
        command.extend(["--actor-records", str(actor_records)])
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True, stdout=subprocess.DEVNULL)
    data = output.read_bytes()
    header_end = data.find(b"\n255\n")
    assert header_end >= 0
    return data[header_end + len(b"\n255\n"):]


def pixel(data: bytes, x: int, y: int) -> tuple[int, int, int]:
    offset = (y * 320 + x) * 3
    return tuple(data[offset : offset + 3])


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="openaladdin-actor-render-") as directory:
        directory_path = Path(directory)
        with_actor = render(ACTORS, directory_path / "with-actor.ppm")
        without_actor = render(None, directory_path / "without-actor.ppm")
        apple_records = directory_path / "apple-actor.tsv"
        apple_records.write_text(
            "# openaladdin-actor-table-v1\n"
            "# Type-0x40 apple palette fixture.\n"
            "# slot type x y movement_pc frame_ptr animation_pc flags facing_x_flip facing_y_flip movement_command_timer movement_loop_pc movement_loop_timer movement_return_pc movement_word_18 movement_word_1a sprite_attribute\n"
            "5 0x40 0x0530 0x0340 0x0 0x001F84A4 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x6000\n",
            encoding="utf-8",
        )
        with_apple = render(apple_records, directory_path / "with-apple.ppm")

    assert with_actor != without_actor, "active actor did not affect the framebuffer"

    # The captured type-0x0A guard uses CRAM line 2. Its purple tunic is a
    # stable palette witness in the fixture's actor bounds; line 0 produces
    # the level's red/brown/orange colours instead.
    assert any(
        pixel(with_actor, x, y) == (116, 87, 144)
        for y in range(30, 161)
        for x in range(20, 91)
    ), "guard actor was rendered with the environment palette"
    # The player is drawn after actors and covers (49, 74) in this fixture;
    # use the adjacent guard pixel as the fixed origin witness.
    assert pixel(with_actor, 47, 74) == (116, 87, 144), (
        "guard visual origin is not aligned with the Genesis actor origin"
    )

    # The apple's red-violet outline comes from palette line 3. If it is
    # rendered with the guard line, the same source pixels become solid red.
    assert any(
        pixel(with_apple, x, y) == (144, 0, 52)
        for y in range(90, 131)
        for x in range(35, 71)
    ), "apple actor was rendered with the guard palette"

    print("native actor render: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
