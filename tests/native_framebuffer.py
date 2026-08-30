#!/usr/bin/env python3
"""Smoke-test the deterministic native framebuffer audit output."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from genie.analysis.visual_diff import read_image


BINARY = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/openaladdin"


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="openaladdin-framebuffer-") as directory:
        images = []
        for index in range(2):
            output = Path(directory) / f"frame-{index}.ppm"
            result = subprocess.run(
                [
                    str(BINARY),
                    "--no-window",
                    "--frames",
                    "1",
                    "--framebuffer-out",
                    str(output),
                ],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )
            assert result.returncode == 0, result.stderr
            image = read_image(output)
            assert image[:2] == (320, 224)
            assert any(image[2]), "native framebuffer is empty"
            images.append(image)
        assert images[0] == images[1], "native framebuffer is not deterministic"
    print("native framebuffer: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
