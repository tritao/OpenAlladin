#!/usr/bin/env python3
"""Decode the original Aladdin animation bytecode into inspectable JSON.

The game stores animation programs as a stream of big-endian words, with a
special convention used by the 68K interpreter: bytes in the range EA..FE are
commands, while all other entries are 16-bit frame references.  This tool
keeps the original bytes and addresses in the output so the result can be
checked directly against the ROM.

This is intentionally a decoder, not an interpreter.  Conditional and
state-dependent commands are represented with their raw operands and any
statically visible branch target; no game state is guessed during conversion.
With --discover-streams it also scans 68K absolute-long references for likely
animation entry points outside the six named player streams.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path
from typing import Any


COMMAND_FIRST = 0xEA
COMMAND_LAST = 0xFE

PLAYER_STREAMS = {
    "PLAYER_ANIM_IDLE": 0x00121D9A,
    "PLAYER_ANIM_RUN": 0x00122006,
    "PLAYER_ANIM_JUMP": 0x001221B0,
    "PLAYER_ANIM_THROW_APPLE": 0x001223DA,
    "PLAYER_ANIM_SWORD": 0x0012271A,
    "PLAYER_ANIM_BRAKE": 0x001232E0,
}

VM_DISPATCH_TABLE = 0x00004954

# The animation data is in the 0x12xxxx ROM area for this revision.  These
# ranges are defaults for discovery only; callers can override them when
# analyzing another revision or a larger data set.
DEFAULT_STREAM_REGION = (0x00120000, 0x00130000)
DEFAULT_FRAME_REGION = (0x001E0000, 0x00200000)


def hex_address(value: int | None) -> str | None:
    if value is None:
        return None
    return f"0x{value:08X}"


class RomReader:
    def __init__(self, data: bytes):
        self.data = data

    def has(self, address: int, size: int = 1) -> bool:
        return 0 <= address <= len(self.data) and 0 <= size <= len(self.data) - address

    def u8(self, address: int) -> int:
        if not self.has(address, 1):
            raise ValueError(f"ROM read outside image: 0x{address:08X}")
        return self.data[address]

    def u16(self, address: int) -> int:
        if not self.has(address, 2):
            raise ValueError(f"ROM read outside image: 0x{address:08X}")
        return struct.unpack_from(">H", self.data, address)[0]

    def u32(self, address: int) -> int:
        if not self.has(address, 4):
            raise ValueError(f"ROM read outside image: 0x{address:08X}")
        return struct.unpack_from(">I", self.data, address)[0]

    def slice(self, address: int, size: int) -> bytes:
        if not self.has(address, size):
            raise ValueError(f"ROM read outside image: 0x{address:08X} (+{size})")
        return self.data[address : address + size]


class AnimationDecoder:
    """Lossless linear decoder for the original animation stream format."""

    # Entries in the original dispatch table are 32-bit 68K addresses.  The
    # names are based on the handlers identified in the ROM disassembly.
    COMMANDS: dict[int, tuple[str, str]] = {
        0xEA: ("jump", "pointer"),
        0xEB: ("set_actor_flag", "byte"),
        0xEC: ("clear_actor_state", "byte"),
        0xED: ("write_value", "memory_write"),
        0xEE: ("set_frame_timer_or_field", "byte"),
        0xEF: ("advance_frame_delay", "none"),
        0xF0: ("if_random", "pointer"),
        0xF1: ("offset_actor", "signed_word"),
        0xF2: ("if_flag_bit", "flag_bit_pointer"),
        0xF3: ("play_sound", "byte"),
        0xF4: ("if_compare", "compare_pointer"),
        0xF5: ("spawn_or_copy_actor", "opaque"),
        0xF6: ("destroy_or_clear_actor", "byte"),
        0xF7: ("face_or_clear_flip", "none"),
        0xF8: ("select_state_stream", "dynamic"),
        0xF9: ("offset_sprite", "signed_bytes"),
        0xFA: ("add_or_subtract", "arithmetic"),
        0xFB: ("push_parameter", "long"),
        0xFC: ("call_or_return_stream", "dynamic_pointer"),
        0xFD: ("if_player_within_x", "distance_pointer"),
        0xFE: ("if_player_within_y", "distance_pointer"),
    }

    def __init__(self, rom: RomReader):
        self.rom = rom

    def decode_frame(self, address: int) -> dict[str, Any]:
        value = self.rom.u16(address)
        frame: dict[str, Any] = {
            "kind": "frame_ref",
            "address": hex_address(address),
            "size": 2,
            "raw": self.rom.slice(address, 2).hex().upper(),
            "reference": f"0x{value:04X}",
        }

        # The VM resolves a frame word through a 32-bit ROM pointer table.
        # Keep unresolved references explicit instead of treating arbitrary
        # data as a valid frame pointer.
        if self.rom.has(value, 4):
            frame_pointer = self.rom.u32(value)
            if self.rom.has(frame_pointer, 1):
                frame["resolved_frame"] = hex_address(frame_pointer)
            else:
                frame["resolved_frame"] = None
                frame["resolution"] = "pointer_outside_rom"
        else:
            frame["resolved_frame"] = None
            frame["resolution"] = "reference_outside_rom"

        return frame

    def command_size(self, address: int, opcode: int) -> int:
        """Return the size observed by the original VM for one command."""
        if opcode in (0xEA, 0xF0, 0xFC, 0xFD, 0xFE):
            # opcode byte + one byte operand in some cases + 32-bit pointer.
            if opcode == 0xFC and self.rom.has(address, 2):
                # FC 80 is the return form; the handler restores A2 and does
                # not consume an inline pointer.
                return 2 if self.rom.u8(address + 1) & 0x80 else 6
            return 6
        if opcode in (0xEB, 0xEC, 0xEE, 0xF3, 0xF6):
            return 2
        if opcode == 0xED:
            # ED, width/target flags, 16-bit address, then a word or long
            # value.  Even the byte-write path consumes a word operand: the
            # original handler reads a word and stores its low byte.
            if not self.rom.has(address, 4):
                return 4
            mode = self.rom.u8(address + 1) & 0x0F
            value_size = 2 if mode in (1, 2) else 4
            return 4 + value_size
        if opcode == 0xEF:
            return 2
        if opcode == 0xF1:
            return 4
        if opcode == 0xF2:
            return 8
        if opcode == 0xF4:
            # F4, flags, target word, compare value, branch pointer.  Modes 1
            # and 2 compare a word; the remaining modes compare a long.
            if not self.rom.has(address, 2):
                return 2
            mode = self.rom.u8(address + 1) & 0x07
            value_size = 2 if mode in (1, 2) else 4
            return 8 + value_size
        if opcode == 0xF5:
            # The spawn/copy handler advances A2 by fourteen bytes after the
            # low operand byte: six bytes of command header plus ten payload.
            return 16
        if opcode == 0xF7:
            return 2
        if opcode == 0xF8:
            return 2
        if opcode == 0xF9:
            return 4
        if opcode == 0xFA:
            if not self.rom.has(address, 4):
                return 4
            mode = self.rom.u8(address + 1) & 0x3F
            # The byte arithmetic path still consumes a word operand.
            value_size = 2 if mode in (1, 2) else 4
            return 4 + value_size
        if opcode == 0xFB:
            return 6
        raise ValueError(f"Unknown animation opcode 0x{opcode:02X}")

    def decode_command(self, address: int, opcode: int) -> dict[str, Any]:
        name, operand_kind = self.COMMANDS[opcode]
        size = self.command_size(address, opcode)
        if not self.rom.has(address, size):
            size = len(self.rom.data) - address
            truncated = True
        else:
            truncated = False

        raw = self.rom.slice(address, size)
        instruction: dict[str, Any] = {
            "kind": "command",
            "address": hex_address(address),
            "size": size,
            "opcode": f"0x{opcode:02X}",
            "name": name,
            "operand_kind": operand_kind,
            "raw": raw.hex().upper(),
        }
        if truncated:
            instruction["truncated"] = True

        if opcode in (0xEA, 0xF0, 0xFC, 0xFD, 0xFE) and len(raw) >= 6:
            instruction["operand_byte"] = f"0x{raw[1]:02X}"
            target = struct.unpack_from(">I", raw, 2)[0]
            instruction["branch_target"] = hex_address(target)
        elif opcode == 0xF4 and len(raw) >= 6:
            mode = raw[1] & 0x07
            value_size = 2 if mode in (1, 2) else 4
            instruction["compare_fields"] = [f"0x{byte:02X}" for byte in raw[1:4]]
            instruction["compare_value"] = raw[4 : 4 + value_size].hex().upper()
            pointer_offset = 4 + value_size
            if len(raw) >= pointer_offset + 4:
                instruction["branch_target"] = hex_address(
                    struct.unpack_from(">I", raw, pointer_offset)[0]
                )
        elif opcode == 0xF2 and len(raw) >= 8:
            instruction["flag_fields"] = [f"0x{byte:02X}" for byte in raw[1:4]]
            instruction["branch_target"] = hex_address(struct.unpack_from(">I", raw, 4)[0])
        elif opcode == 0xED and len(raw) >= 4:
            instruction["target_fields"] = [f"0x{byte:02X}" for byte in raw[1:4]]
            instruction["value"] = raw[4:].hex().upper()
        elif opcode == 0xFA and len(raw) >= 5:
            instruction["target_fields"] = [f"0x{byte:02X}" for byte in raw[1:4]]
            instruction["value"] = raw[4:].hex().upper()
        elif opcode in (0xEB, 0xEC, 0xEE, 0xF3, 0xF6) and len(raw) >= 2:
            instruction["value"] = f"0x{raw[1]:02X}"
        elif opcode == 0xF1 and len(raw) >= 4:
            instruction["axis_or_flags"] = f"0x{raw[1]:02X}"
            instruction["delta"] = struct.unpack_from(">h", raw, 2)[0]
        elif opcode == 0xF9 and len(raw) >= 4:
            instruction["delta_x"] = struct.unpack_from(">b", raw, 1)[0]
            instruction["delta_y"] = struct.unpack_from(">b", raw, 2)[0]
            instruction["flags"] = f"0x{raw[3]:02X}"
        elif opcode == 0xFB and len(raw) >= 6:
            instruction["parameter"] = hex_address(struct.unpack_from(">I", raw, 2)[0])
        elif opcode == 0xF5 and len(raw) >= 6:
            instruction["template"] = hex_address(struct.unpack_from(">I", raw, 2)[0])

        if opcode == 0xF8:
            instruction["control_flow"] = "dynamic_state_selection"
        elif opcode == 0xFC:
            instruction["control_flow"] = "call_or_return"

        return instruction

    def decode_stream(
        self,
        entry: int,
        max_instructions: int,
        max_bytes: int,
        follow_control_flow: bool,
    ) -> dict[str, Any]:
        instructions: list[dict[str, Any]] = []
        address = entry
        end = min(len(self.rom.data), entry + max_bytes)
        visited: set[int] = set()
        stopped_reason = "instruction_limit"

        for _ in range(max_instructions):
            if address >= end:
                stopped_reason = "byte_limit"
                break
            if address in visited:
                stopped_reason = "control_flow_cycle"
                break
            visited.add(address)
            if not self.rom.has(address, 2):
                stopped_reason = "truncated_word"
                break

            first_byte = self.rom.u8(address)
            try:
                if COMMAND_FIRST <= first_byte <= COMMAND_LAST:
                    instruction = self.decode_command(address, first_byte)
                else:
                    instruction = self.decode_frame(address)
            except ValueError as error:
                instructions.append(
                    {
                        "kind": "error",
                        "address": hex_address(address),
                        "message": str(error),
                    }
                )
                stopped_reason = "decode_error"
                break

            instructions.append(instruction)
            size = instruction["size"]
            if size <= 0:
                stopped_reason = "invalid_size"
                break
            address += size

            if instruction.get("truncated"):
                stopped_reason = "truncated_command"
                break

            if instruction.get("kind") == "command":
                opcode = int(instruction["opcode"], 16)
                instruction_address = int(instruction["address"], 16)
                target_text = instruction.get("branch_target")
                target = int(target_text, 16) if target_text else None

                if opcode == 0xF8:
                    stopped_reason = "dynamic_state_selection"
                    break
                if opcode == 0xEA:
                    if not follow_control_flow or target is None:
                        stopped_reason = "unconditional_jump"
                        break
                    address = target
                    continue
                if opcode == 0xFC:
                    if self.rom.u8(instruction_address + 1) & 0x80:
                        stopped_reason = "dynamic_return"
                        break
                    if not follow_control_flow or target is None:
                        stopped_reason = "dynamic_call"
                        break
                    address = target
                    continue
        else:
            stopped_reason = "instruction_limit"

        return {
            "entry": hex_address(entry),
            "bytes_decoded": sum(
                instruction.get("size", 0) for instruction in instructions
            ),
            "instructions": instructions,
            "stopped_reason": stopped_reason,
        }


def parse_range(value: str) -> tuple[int, int]:
    try:
        start, end = value.split(":", 1)
        parsed = int(start, 0), int(end, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            f"range must be START:END, got {value!r}"
        ) from error
    if parsed[0] < 0 or parsed[1] <= parsed[0]:
        raise argparse.ArgumentTypeError(f"invalid range {value!r}")
    return parsed


def pointer_prefix(rom: RomReader, address: int) -> str | None:
    """Classify common 68K instructions carrying an absolute long pointer."""
    if address < 2 or not rom.has(address - 2, 2):
        return None
    opcode = rom.u16(address - 2)

    # LEA An,xxx.L and MOVEA.L #xxx,An encode the 32-bit value immediately
    # after the two-byte opcode.  The other forms are common absolute-long
    # pointer uses seen around the animation state tables.
    if opcode & 0xF1FF == 0x41F9:
        return "lea_abs_l"
    if opcode & 0xF1FF == 0x207C:
        return "movea_imm_l"
    if opcode & 0xF1FF == 0x203C:
        return "move_imm_l"
    if opcode in (0x23FC, 0x2F3C, 0x4879, 0x4EB9, 0x4EF9):
        return f"opcode_0x{opcode:04X}"
    return None


def discover_stream_entries(
    rom: RomReader,
    decoder: AnimationDecoder,
    stream_region: tuple[int, int],
    frame_region: tuple[int, int],
) -> list[dict[str, Any]]:
    """Find likely animation entry points referenced by 68K code.

    This deliberately discovers *entry points*, not every internal label.
    Command branches inside the stream area are ignored as roots because they
    do not have an absolute-long code reference prefix.  A candidate must
    start with a real frame pointer and keep its first probe records aligned;
    this filters out adjacent text, tile, and compressed data that happens to
    contain values from the frame pointer table.
    """
    stream_start, stream_end = stream_region
    frame_start, frame_end = frame_region
    references: dict[int, list[dict[str, Any]]] = {}

    for address in range(0, len(rom.data) - 3):
        target = rom.u32(address)
        if target & 1 or not stream_start <= target < stream_end:
            continue
        prefix = pointer_prefix(rom, address)
        if prefix is None:
            continue
        if stream_start <= address < stream_end:
            continue
        references.setdefault(target, []).append(
            {
                "address": hex_address(address),
                "instruction": hex_address(address - 2),
                "kind": prefix,
            }
        )

    known_names = {address: name for name, address in PLAYER_STREAMS.items()}
    discovered: list[dict[str, Any]] = []

    for entry, refs in references.items():
        probe = decoder.decode_stream(entry, 32, 256, True)
        instructions = probe["instructions"]
        if not instructions or instructions[0]["kind"] != "frame_ref":
            continue

        first_pointer = instructions[0].get("resolved_frame")
        if first_pointer is None:
            continue
        first_pointer_value = int(first_pointer, 16)
        if not frame_start <= first_pointer_value < frame_end:
            continue

        probe_window = instructions[: min(8, len(instructions))]
        aligned = True
        for instruction in probe_window:
            if instruction["kind"] == "error":
                aligned = False
                break
            if instruction["kind"] == "frame_ref":
                pointer = instruction.get("resolved_frame")
                if pointer is None or not frame_start <= int(pointer, 16) < frame_end:
                    aligned = False
                    break
        if not aligned:
            continue

        frame_count = sum(
            1
            for instruction in instructions
            if instruction["kind"] == "frame_ref"
            and instruction.get("resolved_frame") is not None
        )
        command_count = sum(
            1 for instruction in instructions if instruction["kind"] == "command"
        )
        if frame_count < 2 or command_count < 1:
            continue

        score = min(len(refs), 8) * 10 + min(frame_count, 24) + min(command_count, 24) * 2
        discovered.append(
            {
                "name": known_names.get(entry, f"ANIM_STREAM_{entry:06X}"),
                "entry": hex_address(entry),
                "score": score,
                "references": sorted(refs, key=lambda item: item["address"]),
                "probe": {
                    "instructions": len(instructions),
                    "frames": frame_count,
                    "commands": command_count,
                    "stopped_reason": probe["stopped_reason"],
                },
            }
        )

    # Known roots remain visible even if a future ROM revision changes the
    # code reference form used to select them.
    known_entries = {entry["entry"] for entry in discovered}
    for name, address in PLAYER_STREAMS.items():
        if hex_address(address) in known_entries:
            continue
        discovered.append(
            {
                "name": name,
                "entry": hex_address(address),
                "score": 0,
                "references": [],
                "probe": {"instructions": 0, "frames": 0, "commands": 0},
            }
        )

    return sorted(discovered, key=lambda item: int(item["entry"], 16))


def build_dispatch_table(rom: RomReader) -> dict[str, str | None]:
    result: dict[str, str | None] = {}
    for opcode in range(COMMAND_FIRST, COMMAND_LAST + 1):
        address = VM_DISPATCH_TABLE + (opcode - COMMAND_FIRST) * 4
        if rom.has(address, 4):
            result[f"0x{opcode:02X}"] = hex_address(rom.u32(address))
        else:
            result[f"0x{opcode:02X}"] = None
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "rom",
        nargs="?",
        type=Path,
        default=Path("rom/Disneys_Aladdin_U_p1.bin"),
        help="raw Genesis ROM image",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("build/re/animation_streams.json"),
        help="JSON output path",
    )
    parser.add_argument("--max-instructions", type=int, default=256)
    parser.add_argument("--max-bytes", type=int, default=4096)
    parser.add_argument(
        "--discover-streams",
        action="store_true",
        help="discover and decode additional animation roots referenced by 68K code",
    )
    parser.add_argument(
        "--stream-region",
        type=parse_range,
        default=DEFAULT_STREAM_REGION,
        metavar="START:END",
        help="ROM range to search for stream roots",
    )
    parser.add_argument(
        "--frame-region",
        type=parse_range,
        default=DEFAULT_FRAME_REGION,
        metavar="START:END",
        help="ROM range expected for resolved sprite frame data",
    )
    parser.add_argument(
        "--follow-control-flow",
        action="store_true",
        help="follow direct EA/FC targets; otherwise stop at unconditional control flow",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rom_path: Path = args.rom
    output_path: Path = args.output
    data = rom_path.read_bytes()
    rom = RomReader(data)
    decoder = AnimationDecoder(rom)

    discovery = None
    if args.discover_streams:
        discovery = discover_stream_entries(
            rom,
            decoder,
            args.stream_region,
            args.frame_region,
        )
        streams = {
            candidate["name"]: decoder.decode_stream(
                int(candidate["entry"], 16),
                args.max_instructions,
                args.max_bytes,
                args.follow_control_flow,
            )
            for candidate in discovery
        }
    else:
        streams = {
            name: decoder.decode_stream(
                address,
                args.max_instructions,
                args.max_bytes,
                args.follow_control_flow,
            )
            for name, address in PLAYER_STREAMS.items()
        }
    output = {
        "rom": {
            "path": str(rom_path),
            "size": len(data),
            "crc32": f"{zlib.crc32(data) & 0xFFFFFFFF:08X}",
            "sha1": hashlib.sha1(data).hexdigest().upper(),
        },
        "vm": {
            "command_range": ["0xEA", "0xFE"],
            "dispatch_table": hex_address(VM_DISPATCH_TABLE),
            "dispatch": build_dispatch_table(rom),
            "format": "big_endian_words_with_command_byte_records",
            "follow_control_flow": args.follow_control_flow,
        },
        "streams": streams,
    }
    if discovery is not None:
        output["discovery"] = {
            "stream_region": [hex_address(value) for value in args.stream_region],
            "frame_region": [hex_address(value) for value in args.frame_region],
            "candidate_count": len(discovery),
            "candidates": discovery,
        }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    total = sum(len(stream["instructions"]) for stream in streams.values())
    print(f"decoded {len(streams)} streams, {total} instructions -> {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
