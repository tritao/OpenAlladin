#!/usr/bin/env python3
"""Smoke-test the deterministic native framebuffer audit output."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from genie.analysis.visual_diff import compare, read_image


BINARY = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/openaladdin"


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="openaladdin-framebuffer-") as directory:
        output = Path(directory) / "frame.ppm"
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
        report = compare(image, image, (0, 0, 320, 224))
        assert report["different_pixels"] == 0
    print("native framebuffer: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
