#!/usr/bin/env python3
"""Validate a generated OpenAladdin asset extraction directory."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from common import ROOT, hashes


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, default=ROOT / "build/assets")
    parser.add_argument("--rom", type=Path, default=ROOT / "Disneys_Aladdin_U_p1.bin")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.assets.resolve()
    manifest_path = root / "manifest.json"
    if not manifest_path.exists():
        raise SystemExit(f"asset manifest not found: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    actual = hashes(args.rom.resolve())
    recorded = manifest.get("rom", {})
    for key in ("size", "crc32", "sha1", "sha256"):
        if recorded.get(key) != actual[key]:
            raise SystemExit(f"ROM {key} mismatch: manifest={recorded.get(key)} actual={actual[key]}")

    levels = json.loads((root / "levels.json").read_text(encoding="utf-8"))
    if levels.get("count", 0) < 1:
        raise SystemExit("no levels were extracted")
    rendered_levels = sum(1 for level in levels["levels"] if (level.get("rendered") or {}).get("background"))
    if rendered_levels < 10:
        raise SystemExit(f"only {rendered_levels} level backgrounds were rendered")

    sprites = json.loads((root / "sprites.json").read_text(encoding="utf-8"))
    frame_count = sprites.get("frame_count", 0)
    frame_files = list((root / "sprites/frames").glob("frame*.png"))
    tile_files = list((root / "sprites/tiles").glob("*.SEG"))
    if not sprites.get("supported") or frame_count < 100 or len(frame_files) != frame_count or not tile_files:
        raise SystemExit(
            f"sprite extraction incomplete: supported={sprites.get('supported')} "
            f"metadata={frame_count} png={len(frame_files)} tiles={len(tile_files)}"
        )

    print(f"validated ROM identity: {actual['sha1']}")
    print(f"validated levels: {levels['count']} entries, {rendered_levels} rendered")
    print(f"validated Chopper sprites: {frame_count} frames, {len(tile_files)} tile sets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
