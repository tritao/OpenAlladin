#!/usr/bin/env python3
"""Compare captured Genesis VDP state with the native asset extraction."""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
from typing import Any, Iterable

from openaladdin.common import ROOT


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace-dir", type=Path, default=ROOT / "build/re/traces")
    parser.add_argument("--assets", type=Path, default=ROOT / "build/assets")
    parser.add_argument("--output", type=Path, default=ROOT / "build/re/vdp_asset_comparison.json")
    parser.add_argument("--min-sample", type=int, default=32)
    return parser.parse_args()


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    records = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip():
            records.append(json.loads(line))
    return records


def frame_records(records: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    return [record for record in records if record.get("type") == "frame"]


def read_frame_blocks(path: Path, frame_size: int, frame_count: int) -> list[bytes]:
    if frame_size <= 0 or not path.exists():
        return []
    data = path.read_bytes()
    available = len(data) // frame_size
    count = min(frame_count, available)
    return [data[index * frame_size:(index + 1) * frame_size] for index in range(count)]


def changed_byte_count(previous: bytes | None, current: bytes) -> int | None:
    if previous is None or len(previous) != len(current):
        return None
    return sum(left != right for left, right in zip(previous, current))


def sample_candidates(data: bytes, minimum: int) -> list[tuple[str, bytes]]:
    if len(data) < minimum:
        return []
    sample_size = min(64, len(data))
    candidates = [("prefix", data[:sample_size])]
    if len(data) > sample_size * 2:
        middle = (len(data) - sample_size) // 2
        candidates.append(("middle", data[middle:middle + sample_size]))
    candidates.append(("suffix", data[-sample_size:]))
    # A zero-filled prefix is common in decompressed level data and would
    # match an untouched VDP at offset zero.  Only use samples with enough
    # variation to be useful evidence of an actual upload.
    return [
        (name, sample)
        for name, sample in candidates
        if len(set(sample)) >= 4 and any(sample)
    ]


def find_asset_match(
    asset: bytes,
    memories: dict[str, list[bytes]],
    minimum: int,
) -> dict[str, Any] | None:
    for memory_name, frames in memories.items():
        for frame, memory in enumerate(frames):
            if len(asset) <= len(memory):
                offset = memory.find(asset)
                if offset >= 0:
                    return {
                        "kind": "exact",
                        "memory": memory_name,
                        "frame": frame,
                        "offset": f"0x{offset:X}",
                        "bytes": len(asset),
                    }
            for sample_name, sample in sample_candidates(asset, minimum):
                offset = memory.find(sample)
                if offset >= 0:
                    return {
                        "kind": "sample",
                        "sample": sample_name,
                        "memory": memory_name,
                        "frame": frame,
                        "offset": f"0x{offset:X}",
                        "sample_bytes": len(sample),
                        "asset_bytes": len(asset),
                    }
    return None


def decode_dma_events(writes: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    """Decode the two-word VDP command protocol from the write stream.

    This mirrors the command/address calculation in MAME's 315-5313 VDP.
    The resulting events describe the requested transfer; the frame snapshots
    remain the authoritative record of what actually landed in VDP memory.
    """

    registers = [0] * 0x20
    command_pending = False
    command_part1 = 0
    events = []
    destinations = {0x01: "vram", 0x03: "cram", 0x05: "vsram"}

    for write in writes:
        address = int(write.get("address", 0)) & 0x1F
        data = int(write.get("data", 0)) & 0xFFFF
        if address in (0x00, 0x02):
            command_pending = False
            continue
        if address not in (0x04, 0x06):
            continue

        if (data & 0xC000) == 0x8000:
            reg = (data >> 8) & 0x1F
            if reg < len(registers):
                registers[reg] = data & 0xFF
            continue

        if command_pending:
            command_part2 = data
            command_pending = False
            code = ((command_part1 & 0xC000) >> 14) | ((command_part2 & 0x00F0) >> 2)
            destination = destinations.get(code & 0x0F)
            if code & 0x20 and destination:
                length_words = registers[0x13] | (registers[0x14] << 8)
                source = (
                    registers[0x15]
                    | (registers[0x16] << 8)
                    | ((registers[0x17] & 0x7F) << 16)
                ) << 1
                events.append({
                    "frame": int(write.get("frame", 0)),
                    "pc": int(write.get("pc", 0)),
                    "destination": destination,
                    "code": code,
                    "address": (command_part1 & 0x3FFF) | ((command_part2 & 0x0003) << 14),
                    "source": f"0x{source:06X}",
                    "length_bytes": (length_words << 1) or 0x1FFFF,
                    "length_register": length_words,
                    "dma_type": (registers[0x17] >> 6) & 3,
                    "auto_increment": registers[0x0F],
                })
        else:
            command_part1 = data
            command_pending = True

    return events


def asset_files(root: Path) -> list[Path]:
    return sorted(
        path for path in root.rglob("*")
        if path.is_file() and path.suffix.lower() in {".bin", ".seg"}
    )


def main() -> int:
    args = parse_args()
    trace_dir = args.trace_dir.resolve()
    assets_root = args.assets.resolve()
    records = load_jsonl(trace_dir / "trace_boot.jsonl")
    header = next((record for record in records if record.get("type") == "header"), {})
    frames = frame_records(records)
    vram_size = int(header.get("vdp_vram_bytes", 0))
    cram_size = int(header.get("vdp_cram_bytes", 0))
    vsram_size = int(header.get("vdp_vsram_bytes", 0))
    regs_size = int(header.get("vdp_regs_bytes", 0))

    memories = {
        "vram": read_frame_blocks(trace_dir / "vdp_vram_frames.bin", vram_size, len(frames)),
        "cram": read_frame_blocks(trace_dir / "vdp_cram_frames.bin", cram_size, len(frames)),
        "vsram": read_frame_blocks(trace_dir / "vdp_vsram_frames.bin", vsram_size, len(frames)),
        "registers": read_frame_blocks(trace_dir / "vdp_regs_frames.bin", regs_size, len(frames)),
    }
    memories = {name: values for name, values in memories.items() if values}

    frame_changes = []
    for memory_name, values in memories.items():
        previous = None
        for frame, current in enumerate(values):
            frame_changes.append({
                "memory": memory_name,
                "frame": frame,
                "changed_bytes": changed_byte_count(previous, current),
            })
            previous = current

    writes = load_jsonl(trace_dir / "vdp_writes.jsonl")
    dma_events = decode_dma_events(writes)
    matches = []
    considered = 0
    for path in asset_files(assets_root):
        data = path.read_bytes()
        if len(data) < args.min_sample:
            continue
        considered += 1
        match = find_asset_match(data, memories, args.min_sample)
        if match:
            matches.append({"asset": str(path.relative_to(assets_root)), **match})

    kind_counts = Counter(match["kind"] for match in matches)
    result = {
        "format": "openaladdin-vdp-comparison-v1",
        "trace": {
            "directory": str(trace_dir),
            "frames": len(frames),
            "vdp_device": header.get("vdp_device"),
            "memory_frames": {name: len(values) for name, values in memories.items()},
            "vdp_writes": len(writes),
        },
        "dma": {
            "count": len(dma_events),
            "by_destination": dict(Counter(event["destination"] for event in dma_events)),
            "events": dma_events,
        },
        "native_assets": {
            "root": str(assets_root),
            "considered": considered,
            "matched": len(matches),
            "match_kinds": dict(kind_counts),
            "matches": matches,
        },
        "frame_changes": frame_changes,
    }
    args.output.resolve().parent.mkdir(parents=True, exist_ok=True)
    args.output.resolve().write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

    print(f"runtime frames: {len(frames)}")
    print(f"VDP snapshots: {', '.join(f'{name}={len(values)}' for name, values in memories.items()) or 'none'}")
    print(f"VDP writes: {len(writes)}")
    print(f"DMA events: {len(dma_events)} ({dict(Counter(event['destination'] for event in dma_events))})")
    print(f"native assets matched: {len(matches)}/{considered} ({dict(kind_counts)})")
    print(f"comparison report: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
