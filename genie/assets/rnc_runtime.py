"""Correlate RNC uploads with captured Genesis VRAM and CRAM state."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from genie.platforms.genesis.vdp import render_tileset


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def _frame_blocks(path: Path, frame_size: int, frame_count: int) -> list[bytes]:
    if frame_size <= 0 or not path.exists():
        return []
    data = path.read_bytes()
    count = min(frame_count, len(data) // frame_size)
    return [data[index * frame_size:(index + 1) * frame_size] for index in range(count)]


def _find_all(memory: bytes, needle: bytes, limit: int = 16) -> list[int]:
    if not needle or len(needle) > len(memory):
        return []
    offsets: list[int] = []
    start = 0
    while len(offsets) < limit:
        offset = memory.find(needle, start)
        if offset < 0:
            break
        offsets.append(offset)
        start = offset + 1
    return offsets


def _sample(data: bytes) -> list[tuple[str, bytes]]:
    if len(data) < 32:
        return []
    sample_size = min(64, len(data))
    candidates = [("prefix", data[:sample_size])]
    if len(data) > sample_size * 2:
        middle = (len(data) - sample_size) // 2
        candidates.append(("middle", data[middle:middle + sample_size]))
    candidates.append(("suffix", data[-sample_size:]))
    return [
        (name, sample)
        for name, sample in candidates
        if len(set(sample)) >= 4 and any(sample)
    ]


def _palette_banks(cram: bytes) -> list[bytes]:
    return [cram[index * 32:(index + 1) * 32] for index in range(min(4, len(cram) // 32))]


def _digest(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest().upper()


def analyze_rnc_runtime(
    corpus_root: Path,
    trace_root: Path,
    output_root: Path | None = None,
    load_trace: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Match loader assets to VDP snapshots and render CRAM palette variants."""

    corpus_root = corpus_root.resolve()
    trace_root = trace_root.resolve()
    output_root = (output_root or corpus_root / "runtime").resolve()
    loader_report = json.loads((corpus_root / "loader_analysis.json").read_text(encoding="utf-8"))
    corpus = json.loads((corpus_root / "manifest.json").read_text(encoding="utf-8"))
    blocks = {
        block["offset"]: block
        for block in corpus.get("blocks", [])
        if block.get("decoded")
    }
    records = _read_jsonl(trace_root / "trace_boot.jsonl")
    header = next((record for record in records if record.get("type") == "header"), {})
    frame_count = sum(record.get("type") == "frame" for record in records)
    vram_size = int(header.get("vdp_vram_bytes", 0))
    cram_size = int(header.get("vdp_cram_bytes", 0))
    vram_frames = _frame_blocks(trace_root / "vdp_vram_frames.bin", vram_size, frame_count)
    cram_frames = _frame_blocks(trace_root / "vdp_cram_frames.bin", cram_size, frame_count)
    if not vram_frames:
        raise ValueError(f"trace has no VDP VRAM snapshots: {trace_root}")

    calls_by_block: dict[str, list[dict[str, Any]]] = {}
    for call in loader_report.get("calls", []):
        block = call.get("block")
        if block:
            calls_by_block.setdefault(block["offset"], []).append(call)
    dynamic_by_block: dict[str, list[dict[str, Any]]] = {}
    for event in (load_trace or {}).get("events", []):
        if event.get("block"):
            dynamic_by_block.setdefault(event["source"], []).append(event)

    asset_rows: list[dict[str, Any]] = []
    for offset, calls in sorted(calls_by_block.items(), key=lambda item: int(item[0], 16)):
        block = blocks.get(offset)
        if block is None:
            continue
        asset_data = (corpus_root / block["file"]).read_bytes()
        render_dir = output_root / offset[2:]
        if render_dir.exists():
            for stale in render_dir.glob("palette*.png"):
                stale.unlink()
        expected_destinations = sorted({
            call["destination"]["target"]
            for call in calls
            if call.get("destination")
        })
        expected_destination_values = {int(value, 16) for value in expected_destinations}
        exact_matches: list[dict[str, Any]] = []
        for frame, memory in enumerate(vram_frames):
            for vram_offset in _find_all(memory, asset_data):
                cram = cram_frames[frame] if frame < len(cram_frames) else b""
                exact_matches.append({
                    "frame": frame,
                    "vram_offset": f"0x{vram_offset:04X}",
                    "destination_match": vram_offset in expected_destination_values,
                    "cram_nonzero_bytes": sum(value != 0 for value in cram),
                })
        exact_matches.sort(key=lambda match: (
            not match["destination_match"],
            -match["cram_nonzero_bytes"],
            match["frame"],
            match["vram_offset"],
        ))
        selected = exact_matches[0] if exact_matches else None
        row: dict[str, Any] = {
            "offset": offset,
            "file": block["file"],
            "bytes": len(asset_data),
            "sha1": block.get("sha1"),
            "storage_family": calls[0].get("block", {}).get("storage_family"),
            "call_count": len(calls),
            "call_addresses": [call["call_address"] for call in calls],
            "expected_destinations": expected_destinations,
            "exact_match_count": len(exact_matches),
            "exact_matches": exact_matches[:16],
            "status": "exact" if selected else "not_found",
            "selected_match": selected,
            "dynamic_load_count": len(dynamic_by_block.get(offset, [])),
            "dynamic_loads": dynamic_by_block.get(offset, []),
        }
        if selected and int(selected["frame"]) < len(cram_frames):
            frame = int(selected["frame"])
            cram = cram_frames[frame]
            banks = _palette_banks(cram)
            row["palette"] = {
                "frame": frame,
                "cram_sha1": _digest(cram),
                "status": "observed" if any(cram) else "empty_at_match",
                "banks": [bank.hex().upper() for bank in banks],
            }
            if len(asset_data) >= 32 and len(asset_data) % 32 == 0 and banks and any(cram):
                rendered: list[dict[str, Any]] = []
                for bank_index, bank in enumerate(banks):
                    render_path = render_dir / f"palette{bank_index}.png"
                    render_tileset(asset_data, bank, render_path)
                    rendered.append({
                        "bank": bank_index,
                        "file": str(render_path.relative_to(corpus_root)),
                    })
                row["rendered"] = rendered
        else:
            samples = []
            for frame, memory in enumerate(vram_frames):
                for name, sample in _sample(asset_data):
                    found = memory.find(sample)
                    if found >= 0:
                        samples.append({
                            "frame": frame,
                            "sample": name,
                            "vram_offset": f"0x{found:04X}",
                        })
                        break
                if samples:
                    break
            if samples:
                row["status"] = "sample"
                row["sample_match"] = samples[0]
        asset_rows.append(row)

    exact = [row for row in asset_rows if row["status"] == "exact"]
    family_rows: dict[str, list[dict[str, Any]]] = {}
    for row in asset_rows:
        family_rows.setdefault(row.get("storage_family") or "unassigned", []).append(row)
    families = [
        {
            "storage_family": family,
            "asset_count": len(rows),
            "exact_matches": sum(row["status"] == "exact" for row in rows),
            "observed_palette_matches": sum(row.get("palette", {}).get("status") == "observed" for row in rows),
            "blocks": [row["offset"] for row in rows],
        }
        for family, rows in sorted(family_rows.items())
    ]
    runtime = {
        "format": "openaladdin-rnc-runtime-analysis-v1",
        "rom": loader_report.get("rom", corpus.get("rom", {})),
        "trace": {
            "directory": str(trace_root),
            "frames": frame_count,
            "vram_frames": len(vram_frames),
            "cram_frames": len(cram_frames),
            "vdp_device": header.get("vdp_device"),
        },
        "dynamic_loader_trace": {
            "present": load_trace is not None,
            "log": (load_trace or {}).get("log"),
            "event_count": (load_trace or {}).get("summary", {}).get("event_count", 0),
        },
        "summary": {
            "loader_asset_count": len(asset_rows),
            "exact_match_count": len(exact),
            "sample_match_count": sum(row["status"] == "sample" for row in asset_rows),
            "not_found_count": sum(row["status"] == "not_found" for row in asset_rows),
            "palette_preview_count": sum(bool(row.get("rendered")) for row in asset_rows),
            "dynamic_event_count": sum(len(events) for events in dynamic_by_block.values()),
            "dynamic_block_count": len(dynamic_by_block),
            "dynamic_executed_without_vram_match": sum(
                bool(dynamic_by_block.get(row["offset"])) and row["status"] == "not_found"
                for row in asset_rows
            ),
        },
        "families": families,
        "assets": asset_rows,
    }
    output_path = corpus_root / "runtime_analysis.json"
    output_path.write_text(json.dumps(runtime, indent=2) + "\n", encoding="utf-8")
    return runtime
