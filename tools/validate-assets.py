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

    rnc_manifest_path = root / "rnc/manifest.json"
    if not rnc_manifest_path.exists():
        raise SystemExit(f"RNC corpus manifest not found: {rnc_manifest_path}")
    rnc = json.loads(rnc_manifest_path.read_text(encoding="utf-8"))
    rnc_identity = rnc.get("rom", {})
    for key in ("size", "crc32", "sha1", "sha256"):
        if rnc_identity.get(key) != actual[key]:
            raise SystemExit(
                f"RNC corpus ROM {key} mismatch: "
                f"manifest={rnc_identity.get(key)} actual={actual[key]}"
            )
    blocks = rnc.get("blocks", [])
    decoded = [block for block in blocks if block.get("decoded")]
    if not blocks or len(decoded) != len(blocks) or rnc.get("failed_count", 0):
        raise SystemExit(
            f"RNC corpus incomplete: blocks={len(blocks)} "
            f"decoded={len(decoded)} failed={rnc.get('failed_count')}"
        )
    missing_rnc = [
        block.get("offset")
        for block in decoded
        if not (root / "rnc" / block["file"]).exists()
    ]
    if missing_rnc:
        raise SystemExit(f"RNC decompressed files missing: {missing_rnc[:5]}")
    for block in decoded:
        block_path = root / "rnc" / block["file"]
        block_hashes = hashes(block_path)
        if block_hashes["size"] != block.get("unpacked_bytes"):
            raise SystemExit(f"RNC size mismatch for {block['offset']}: {block_path}")
        if block_hashes["sha1"] != block.get("sha1") or block_hashes["sha256"] != block.get("sha256"):
            raise SystemExit(f"RNC digest mismatch for {block['offset']}: {block_path}")

    classification_path = root / "rnc/classification.json"
    if not classification_path.exists():
        raise SystemExit(f"RNC classification not found: {classification_path}")
    classification = json.loads(classification_path.read_text(encoding="utf-8"))
    classified_blocks = classification.get("blocks", [])
    if len(classified_blocks) != rnc.get("unassigned_count", 0):
        raise SystemExit(
            f"RNC classification count mismatch: "
            f"classified={len(classified_blocks)} "
            f"unassigned={rnc.get('unassigned_count')}"
        )
    missing_renders = [
        block["offset"]
        for block in classified_blocks
        if block.get("rendered")
        and not (root / "rnc" / block["rendered"]["file"]).exists()
    ]
    if missing_renders:
        raise SystemExit(f"RNC classification renders missing: {missing_renders[:5]}")

    references_path = root / "rnc/pointer_references.json"
    if not references_path.exists():
        raise SystemExit(f"RNC pointer report not found: {references_path}")
    pointer_report = json.loads(references_path.read_text(encoding="utf-8"))
    pointer_identity = pointer_report.get("rom", {})
    for key in ("size", "crc32", "sha1", "sha256"):
        if pointer_identity.get(key) != actual[key]:
            raise SystemExit(
                f"RNC pointer report ROM {key} mismatch: "
                f"manifest={pointer_identity.get(key)} actual={actual[key]}"
            )
    if pointer_report.get("target_count") != len(blocks):
        raise SystemExit(
            f"RNC pointer target mismatch: report={pointer_report.get('target_count')} "
            f"corpus={len(blocks)}"
        )
    if pointer_report.get("unassigned_target_count") != rnc.get("unassigned_count"):
        raise SystemExit(
            f"RNC pointer unassigned-target mismatch: "
            f"report={pointer_report.get('unassigned_target_count')} "
            f"corpus={rnc.get('unassigned_count')}"
        )

    print(f"validated ROM identity: {actual['sha1']}")
    print(f"validated levels: {levels['count']} entries, {rendered_levels} rendered")
    print(f"validated Chopper sprites: {frame_count} frames, {len(tile_files)} tile sets")
    print(
        f"validated RNC corpus: {len(blocks)} blocks, "
        f"{rnc.get('unassigned_count', 0)} currently unassigned"
    )
    print(
        f"validated RNC classification: {classification.get('tile_candidates', 0)} "
        f"tile candidates, {len(classification.get('families', []))} families"
    )
    print(
        f"validated RNC pointers: {pointer_report.get('reference_count', 0)} references, "
        f"{pointer_report.get('pointer_table_count', 0)} tables"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
