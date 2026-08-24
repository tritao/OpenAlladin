#!/usr/bin/env python3
"""Inventory actor animation cursors from a frame-by-frame MAME RAM trace.

The actor animation field is a moving cursor, not necessarily the beginning of
an animation program.  This tool therefore reports both the complete cursor
history for every slot and the first non-zero cursor observed in each active
interval.  It also probes every observed cursor against the ROM decoder and
annotates cursors that fall inside a statically decoded stream.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import struct
from collections import defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TRACE = ROOT / "build/re/actor-gameplay"
DEFAULT_ROM = ROOT / "Disneys_Aladdin_U_p1.bin"
DEFAULT_CLASSIFIED = ROOT / "build/re/animation_streams_classified.json"
DEFAULT_DECODED = ROOT / "build/re/animation_streams_all.json"
DEFAULT_OUTPUT = ROOT / "build/re/actor_animation_inventory.json"


def load_decoder():
    path = ROOT / "tools/decode-animation-streams.py"
    spec = importlib.util.spec_from_file_location(
        "openaladdin_animation_decoder", path
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load decoder: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]


def read_u8(data: bytes, offset: int) -> int:
    return data[offset]


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def hex_address(value: int) -> str:
    return f"0x{value:08X}"


def frame_ranges(frames: list[int]) -> list[dict[str, int]]:
    if not frames:
        return []
    ranges = []
    start = previous = frames[0]
    for frame in frames[1:]:
        if frame != previous + 1:
            ranges.append({"first": start, "last": previous, "count": previous - start + 1})
            start = frame
        previous = frame
    ranges.append({"first": start, "last": previous, "count": previous - start + 1})
    return ranges


def build_static_index(
    classified_path: Path,
    decoded_path: Path,
) -> tuple[dict[int, dict[str, Any]], list[dict[str, Any]]]:
    """Index decoded stream instruction ranges by ROM address."""
    if not classified_path.exists():
        return {}, []

    streams = json.loads(classified_path.read_text(encoding="utf-8"))
    if isinstance(streams, dict):
        streams = streams.get("streams", [])
    decoded = {}
    if decoded_path.exists():
        decoded_data = json.loads(decoded_path.read_text(encoding="utf-8"))
        decoded = decoded_data.get("streams", {})

    index: dict[int, dict[str, Any]] = {}
    rows = []
    for stream in streams:
        entry = int(stream["entry"], 16)
        row = {
            "name": stream.get("name"),
            "entry": hex_address(entry),
            "classification": stream.get("classification"),
            "confidence": stream.get("confidence"),
        }
        rows.append(row)
        decoded_stream = decoded.get(stream.get("name"), {})
        instructions = stream.get("instructions", []) or decoded_stream.get(
            "instructions", []
        )
        for instruction in instructions:
            address = int(instruction["address"], 16)
            size = max(1, int(instruction.get("size", 1)))
            for cursor in range(address, address + size, 2):
                index.setdefault(cursor, row)
    return index, rows


def probe_stream(decoder: Any, address: int, frame_region: tuple[int, int]) -> dict[str, Any]:
    """Return a conservative decoder probe for one observed cursor."""
    result = decoder.decode_stream(address, 32, 256, True)
    instructions = result["instructions"]
    frame_count = 0
    command_count = 0
    aligned = bool(instructions)
    for instruction in instructions[:8]:
        if instruction.get("kind") == "command":
            command_count += 1
            continue
        if instruction.get("kind") != "frame_ref":
            aligned = False
            continue
        pointer = instruction.get("resolved_frame")
        if pointer is None:
            aligned = False
            continue
        frame_pointer = int(pointer, 16)
        if not frame_region[0] <= frame_pointer < frame_region[1]:
            aligned = False
        frame_count += 1

    return {
        "first_kind": instructions[0].get("kind") if instructions else None,
        "frames_in_probe": frame_count,
        "commands_in_probe": command_count,
        "aligned_probe": aligned,
        "stopped_reason": result["stopped_reason"],
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace-dir", type=Path, default=DEFAULT_TRACE)
    parser.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    parser.add_argument("--classified", type=Path, default=DEFAULT_CLASSIFIED)
    parser.add_argument("--decoded", type=Path, default=DEFAULT_DECODED)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--frame-region",
        type=lambda value: tuple(int(part, 0) for part in value.split(":", 1)),
        default=(0x001E0000, 0x00200000),
        metavar="START:END",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    trace_dir = args.trace_dir.resolve()
    records = load_jsonl(trace_dir / "trace_boot.jsonl")
    header = next(record for record in records if record.get("type") == "header")
    frame_records = [record for record in records if record.get("type") == "frame"]

    ram_start = int(header["ram_start"])
    ram_size = int(header["ram_size"])
    table_base = int(header.get("actor_table_base", 0xFF7E40))
    stride = int(header.get("actor_stride", 0x42))
    slot_count = int(header.get("actor_slot_count", 32))
    type_offset = int(
        header.get("actor_type_offset", header.get("actor_active_offset", 0x00))
    )
    animation_pc_offset = int(header.get("actor_animation_pc_offset", 0x20))
    table_offset = table_base - ram_start

    ram_path = trace_dir / "ram_frames.bin"
    ram = ram_path.read_bytes()
    expected_size = len(frame_records) * ram_size
    if len(ram) != expected_size:
        raise ValueError(
            f"{ram_path} has {len(ram)} bytes; expected {expected_size} "
            f"for {len(frame_records)} frames"
        )

    decoder_module = load_decoder()
    decoder = decoder_module.AnimationDecoder(
        decoder_module.RomReader(args.rom.read_bytes())
    )
    static_index, static_streams = build_static_index(
        args.classified.resolve(),
        args.decoded.resolve(),
    )

    slot_rows: list[dict[str, Any]] = []
    all_pc_frames: dict[int, list[int]] = defaultdict(list)
    all_pc_slots: dict[int, set[int]] = defaultdict(set)

    for slot in range(slot_count):
        state_frames: dict[tuple[int, int], list[int]] = defaultdict(list)
        type_values: set[int] = set()
        for frame_index, _ in enumerate(frame_records):
            record_offset = table_offset + slot * stride
            frame_offset = frame_index * ram_size
            actor_type = read_u8(ram, frame_offset + record_offset + type_offset)
            animation_pc = read_u32(
                ram,
                frame_offset + record_offset + animation_pc_offset,
            )
            type_values.add(actor_type)
            state_frames[(actor_type, animation_pc)].append(frame_index)
            if animation_pc:
                all_pc_frames[animation_pc].append(frame_index)
                all_pc_slots[animation_pc].add(slot)

        states = []
        for (actor_type, animation_pc), frames in sorted(
            state_frames.items(), key=lambda item: (item[1][0], item[0][0], item[0][1])
        ):
            state: dict[str, Any] = {
                "actor_type": actor_type,
                "active": actor_type,
                "animation_pc": hex_address(animation_pc),
                "frames": frame_ranges(frames),
            }
            if animation_pc:
                state["static_stream"] = static_index.get(animation_pc)
            states.append(state)

        intervals = []
        previous_type = None
        current: dict[str, Any] | None = None
        for frame_index, _ in enumerate(frame_records):
            record_offset = table_offset + slot * stride
            frame_offset = frame_index * ram_size
            actor_type = read_u8(ram, frame_offset + record_offset + type_offset)
            animation_pc = read_u32(
                ram,
                frame_offset + record_offset + animation_pc_offset,
            )
            if actor_type != previous_type:
                if current is not None:
                    current["last_frame"] = frame_index - 1
                    intervals.append(current)
                current = {
                    "first_frame": frame_index,
                    "actor_type": actor_type,
                    "active": actor_type,
                    "first_nonzero_animation_pc": None,
                    "first_nonzero_frame": None,
                }
                previous_type = actor_type
            if current is not None and current["first_nonzero_animation_pc"] is None and animation_pc:
                current["first_nonzero_animation_pc"] = hex_address(animation_pc)
                current["first_nonzero_frame"] = frame_index
        if current is not None:
            current["last_frame"] = len(frame_records) - 1
            intervals.append(current)

        slot_rows.append(
            {
                "slot": slot,
                "record": hex_address(table_base + slot * stride),
                "actor_type_values": sorted(type_values),
                "active_values": sorted(type_values),
                "states": states,
                "active_intervals": intervals,
            }
        )

    observed_rows = []
    for animation_pc in sorted(all_pc_frames):
        static_stream = static_index.get(animation_pc)
        row: dict[str, Any] = {
            "animation_pc": hex_address(animation_pc),
            "first_frame": min(all_pc_frames[animation_pc]),
            "last_frame": max(all_pc_frames[animation_pc]),
            "frame_count": len(set(all_pc_frames[animation_pc])),
            "slots": sorted(all_pc_slots[animation_pc]),
            "static_stream": static_stream,
        }
        if static_stream is None:
            row["decoder_probe"] = probe_stream(
                decoder,
                animation_pc,
                args.frame_region,
            )
        observed_rows.append(row)

    roots = []
    for slot in slot_rows:
        for interval in slot["active_intervals"]:
            if interval["actor_type"] == 0:
                continue
            pointer = interval["first_nonzero_animation_pc"]
            if pointer is None:
                continue
            address = int(pointer, 16)
            roots.append(
                {
                    "slot": slot["slot"],
                    "actor_type": interval["actor_type"],
                    "active": interval["actor_type"],
                    "first_frame": interval["first_frame"],
                    "first_nonzero_frame": interval["first_nonzero_frame"],
                    "animation_pc": pointer,
                    "static_stream": static_index.get(address),
                    "decoder_probe": (
                        None
                        if address in static_index
                        else probe_stream(decoder, address, args.frame_region)
                    ),
                }
            )

    result = {
        "format": "openaladdin-actor-animation-inventory-v1",
        "rom": {
            "path": str(args.rom.resolve()),
            "size": args.rom.stat().st_size,
        },
        "trace": {
            "directory": str(trace_dir),
            "frames": len(frame_records),
        },
        "actor_table": {
            "base": hex_address(table_base),
            "stride": stride,
            "slot_count": slot_count,
            "type_offset": type_offset,
            "active_offset": type_offset,
            "animation_pc_offset": animation_pc_offset,
        },
        "static_streams": static_streams,
        "active_interval_roots": roots,
        "observed_animation_cursors": observed_rows,
        "slots": slot_rows,
    }
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(
        f"analyzed {len(frame_records)} frames, {slot_count} actor slots, "
        f"{len(observed_rows)} cursors -> {output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
