"""Heuristics and renderers for classifying decompressed RNC assets."""

from __future__ import annotations

from collections.abc import Iterable
import json
from pathlib import Path
from typing import Any

from .genesis import render_tileset


TILE_BYTES = 32
DEFAULT_PALETTE_WORDS = (
    0x707, 0x763, 0x751, 0x642, 0x531, 0x420, 0x310, 0x200,
    0x401, 0x347, 0x236, 0x202, 0x444, 0x222, 0x777, 0x000,
)


def _default_palette() -> bytes:
    return b"".join(word.to_bytes(2, "big") for word in DEFAULT_PALETTE_WORDS)


def _tile_metrics(data: bytes) -> dict[str, Any]:
    tiles = [data[offset:offset + TILE_BYTES] for offset in range(0, len(data), TILE_BYTES)]
    blank_tiles = sum(not any(tile) for tile in tiles)
    unique_tiles = len(set(tiles))
    nonzero = sum(byte != 0 for byte in data)
    return {
        "tile_count": len(tiles),
        "blank_tiles": blank_tiles,
        "unique_tiles": unique_tiles,
        "nonzero_ratio": round(nonzero / len(data), 6) if data else 0.0,
        "unique_tile_ratio": round(unique_tiles / len(tiles), 6) if tiles else 0.0,
    }


def _classify_block(block: dict[str, Any], data: bytes) -> dict[str, Any]:
    size = len(data)
    tile_aligned = size >= TILE_BYTES * 8 and size % TILE_BYTES == 0
    result: dict[str, Any] = {
        "offset": block["offset"],
        "file": block["file"],
        "bytes": size,
        "references": block.get("references", []),
        "tile_aligned": tile_aligned,
        "classification": "genesis_tile_candidate" if tile_aligned else "unclassified_binary",
    }
    if tile_aligned:
        result.update(_tile_metrics(data))
    return result


def _contiguous_families(blocks: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    families: list[dict[str, Any]] = []
    current: list[dict[str, Any]] = []
    previous_end: int | None = None

    for block in blocks:
        offset = int(block["offset"], 16)
        total_bytes = int(block.get("total_bytes", 0))
        if current and previous_end != offset:
            families.append(_family(current))
            current = []
        current.append(block)
        previous_end = offset + total_bytes
    if current:
        families.append(_family(current))
    return families


def _family(blocks: list[dict[str, Any]]) -> dict[str, Any]:
    offsets = [int(block["offset"], 16) for block in blocks]
    return {
        "first_offset": f"0x{offsets[0]:06X}",
        "last_offset": f"0x{offsets[-1]:06X}",
        "block_count": len(blocks),
        "unpacked_bytes": sum(int(block.get("unpacked_bytes", 0)) for block in blocks),
        "classifications": sorted({block["classification"] for block in blocks}),
        "blocks": [block["offset"] for block in blocks],
    }


def classify_rnc_corpus(corpus_root: Path) -> dict[str, Any]:
    """Classify unassigned RNC output and render likely Genesis tile blocks."""

    corpus_root = corpus_root.resolve()
    manifest_path = corpus_root / "manifest.json"
    if not manifest_path.exists():
        raise ValueError(f"RNC corpus manifest not found: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    render_root = corpus_root / "classified"
    render_root.mkdir(parents=True, exist_ok=True)
    palette = _default_palette()

    classified: list[dict[str, Any]] = []
    family_blocks: list[dict[str, Any]] = []
    for block in manifest.get("blocks", []):
        if not block.get("decoded") or block.get("references"):
            continue
        block_path = corpus_root / block["file"]
        if not block_path.exists():
            raise ValueError(f"RNC block file not found: {block_path}")
        result = _classify_block(block, block_path.read_bytes())
        if result["tile_aligned"]:
            output_path = render_root / f"{block['offset'][2:]}.png"
            render_tileset(block_path.read_bytes(), palette, output_path)
            result["rendered"] = {
                "palette": "chopper-default",
                "file": str(output_path.relative_to(corpus_root)),
            }
        classified.append(result)
        family_blocks.append({**block, **result})

    families = _contiguous_families(family_blocks)
    tile_candidates = [
        block for block in classified if block["classification"] == "genesis_tile_candidate"
    ]
    result = {
        "format": "openaladdin-rnc-classification-v1",
        "palette": {
            "name": "chopper-default",
            "words": [f"0x{word:03X}" for word in DEFAULT_PALETTE_WORDS],
            "note": "Evidence palette only; runtime palette selection remains unconfirmed.",
        },
        "unassigned_blocks": len(classified),
        "tile_candidates": len(tile_candidates),
        "binary_candidates": len(classified) - len(tile_candidates),
        "families": families,
        "blocks": classified,
    }
    output_path = corpus_root / "classification.json"
    output_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result
