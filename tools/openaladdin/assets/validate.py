#!/usr/bin/env python3
"""Validate a generated OpenAladdin asset extraction directory."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from openaladdin.common import ROOT, hashes


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, default=ROOT / "build/assets")
    parser.add_argument("--rom", type=Path, default=ROOT / "rom/Disneys_Aladdin_U_p1.bin")
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

    family_path = root / "rnc/family_analysis.json"
    if not family_path.exists():
        raise SystemExit(f"RNC family analysis not found: {family_path}")
    family_analysis = json.loads(family_path.read_text(encoding="utf-8"))
    family_identity = family_analysis.get("rom", {})
    for key in ("size", "crc32", "sha1", "sha256"):
        if family_identity.get(key) != actual[key]:
            raise SystemExit(
                f"RNC family-analysis ROM {key} mismatch: "
                f"manifest={family_identity.get(key)} actual={actual[key]}"
            )
    family_blocks = family_analysis.get("blocks", [])
    if len(family_blocks) != rnc.get("unassigned_count"):
        raise SystemExit(
            f"RNC family-analysis block mismatch: report={len(family_blocks)} "
            f"corpus={rnc.get('unassigned_count')}"
        )
    if family_analysis.get("summary", {}).get("reference_count") != pointer_report.get(
        "unassigned_reference_count"
    ):
        raise SystemExit(
            "RNC family-analysis reference mismatch: "
            f"report={family_analysis.get('summary', {}).get('reference_count')} "
            f"pointers={pointer_report.get('unassigned_reference_count')}"
        )
    if family_analysis.get("summary", {}).get("storage_family_count") != len(
        classification.get("families", [])
    ):
        raise SystemExit("RNC family-analysis storage-family mismatch")

    loader_path = root / "rnc/loader_analysis.json"
    if not loader_path.exists():
        raise SystemExit(f"RNC loader analysis not found: {loader_path}")
    loader_analysis = json.loads(loader_path.read_text(encoding="utf-8"))
    loader_identity = loader_analysis.get("rom", {})
    for key in ("size", "crc32", "sha1", "sha256"):
        if loader_identity.get(key) != actual[key]:
            raise SystemExit(
                f"RNC loader-analysis ROM {key} mismatch: "
                f"manifest={loader_identity.get(key)} actual={actual[key]}"
            )
    resolved_loader_calls = [
        call for call in loader_analysis.get("calls", [])
        if call.get("status") == "resolved_rnc"
    ]
    if not resolved_loader_calls:
        raise SystemExit("RNC loader analysis found no resolved uploads")
    missing_loader_blocks = [
        call["block"]["offset"]
        for call in resolved_loader_calls
        if call.get("block", {}).get("offset") not in {block["offset"] for block in blocks}
    ]
    if missing_loader_blocks:
        raise SystemExit(f"RNC loader references missing corpus blocks: {missing_loader_blocks[:5]}")

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
    print(
        f"validated RNC family analysis: "
        f"{family_analysis.get('summary', {}).get('storage_family_count', 0)} storage families, "
        f"{family_analysis.get('summary', {}).get('code_cluster_count', 0)} code clusters"
    )
    print(
        f"validated RNC loaders: {loader_analysis.get('summary', {}).get('rnc_loader_call_count', 0)} "
        f"uploads, {loader_analysis.get('summary', {}).get('resolved_destination_count', 0)} destinations"
    )
    runtime_path = root / "rnc/runtime_analysis.json"
    if runtime_path.exists():
        runtime_analysis = json.loads(runtime_path.read_text(encoding="utf-8"))
        runtime_identity = runtime_analysis.get("rom", {})
        for key in ("size", "crc32", "sha1", "sha256"):
            if runtime_identity.get(key) != actual[key]:
                raise SystemExit(
                    f"RNC runtime-analysis ROM {key} mismatch: "
                    f"manifest={runtime_identity.get(key)} actual={actual[key]}"
                )
        missing_runtime_renders = [
            render["file"]
            for asset in runtime_analysis.get("assets", [])
            for render in asset.get("rendered", [])
            if not (root / "rnc" / render["file"]).exists()
        ]
        if missing_runtime_renders:
            raise SystemExit(
                f"RNC runtime palette previews missing: {missing_runtime_renders[:5]}"
            )
        print(
            f"validated RNC runtime correlation: "
            f"{runtime_analysis.get('summary', {}).get('exact_match_count', 0)} exact VRAM matches, "
            f"{runtime_analysis.get('summary', {}).get('palette_preview_count', 0)} palette previews"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
