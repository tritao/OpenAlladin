#!/usr/bin/env python3
"""Decode Aladdin actor movement state streams into lossless JSON.

Movement streams share the animation VM command handlers, but use signed
command bytes 0x80..0x94.  The common movement update consumes two signed
delta bytes first, then executes commands until the next byte is outside that
range.  The command byte is mapped to the corresponding animation handler by
adding 0x6A (0x80 -> 0xEA, ..., 0x94 -> 0xFE).

This tool records the raw bytes, decoded step deltas, shared handler mapping,
operands, and statically visible control-flow targets.  It does not guess
runtime actor state for conditional or dynamic commands.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import struct
from pathlib import Path
from typing import Any


# This module is also executable as a script, so keep a repository root for
# command-line defaults without importing the CLI layer.
ROOT = Path(__file__).resolve().parents[4]
MOVEMENT_FIRST = 0x80
MOVEMENT_LAST = 0x94
ANIMATION_OFFSET = 0x6A
MOVEMENT_DISPATCH_TABLE = 0x00004954

MOVEMENT_EFFECTS = {
    0x80: "jump_to_stream_pointer",
    0x81: "toggle_actor_facing",
    0x82: "clear_animation_or_movement_cursor",
    0x83: "write_actor_or_ram_value",
    0x84: "set_movement_loop_or_command_timer",
    0x85: "rewind_movement_loop_after_timer",
    0x86: "random_branch",
    0x87: "add_signed_actor_offset",
    0x88: "test_actor_or_ram_flag",
    0x89: "play_sound_or_event",
    0x8A: "compare_actor_or_ram_value",
    0x8B: "spawn_or_copy_actor",
    0x8C: "destroy_or_clear_actor",
    0x8D: "face_toward_player",
    0x8E: "select_dynamic_state_stream",
    0x8F: "adjust_actor_velocity_or_position",
    0x90: "arithmetic_or_compare_memory",
    0x91: "push_movement_parameter",
    0x92: "call_or_return_stream",
    0x93: "if_player_within_x",
    0x94: "if_player_within_y",
}

DEFAULT_ENTRIES = {
    "ACTOR_MOVE_STATE_11F6D4": 0x0011F6D4,
    "ACTOR_MOVE_STATE_11F6FE": 0x0011F6FE,
    "ACTOR_MOVE_SCENE_RESOURCE_DEFAULT": 0x0011F728,
    "ACTOR_MOVE_TRANSITION_PRESENTATION_LEAD_IN": 0x00120360,
    "ACTOR_MOVE_STATE_1203C0": 0x001203C0,
    "ACTOR_MOVE_STATE_120432": 0x00120432,
    "ACTOR_MOVE_TYPE1E_STATE46_RESPONSE": 0x0012046C,
    "ACTOR_MOVE_TYPE1E_PROXIMITY_TRANSITION_GATE": 0x001204E2,
    "ACTOR_MOVE_TYPE2A_VERTICAL_BOB": 0x00120F76,
    "ACTOR_MOVE_STATE_121240": 0x00121240,
    "ACTOR_MOVE_STATE_1217B4": 0x001217B4,
}


def load_animation_decoder():
    path = ROOT / "genie/games/aladdin/vm/animation.py"
    spec = importlib.util.spec_from_file_location("openaladdin_animation_decoder", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load animation decoder: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def hex_address(value: int | None) -> str | None:
    if value is None:
        return None
    return f"0x{value:08X}"


def signed_byte(value: int) -> int:
    return value - 0x100 if value & 0x80 else value


class MovementDecoder:
    def __init__(self, rom: Any):
        animation_module = load_animation_decoder()
        self.animation = animation_module.AnimationDecoder(rom)
        self.rom = rom

    @staticmethod
    def animation_opcode(opcode: int) -> int:
        return opcode + ANIMATION_OFFSET

    def decode_command(self, address: int, opcode: int) -> dict[str, Any]:
        shared_opcode = self.animation_opcode(opcode)
        row = self.animation.decode_command(address, shared_opcode)
        row["opcode"] = f"0x{opcode:02X}"
        row["shared_animation_opcode"] = f"0x{shared_opcode:02X}"
        row["context"] = "movement_vm"
        row["movement_effect"] = MOVEMENT_EFFECTS[opcode]
        return row

    def dispatch_table(self) -> dict[str, dict[str, str | None]]:
        result = {}
        for opcode in range(MOVEMENT_FIRST, MOVEMENT_LAST + 1):
            # The 68K interpreter sign-extends the movement opcode before
            # adding 0x80, so 0x80 maps to table index zero.
            table_index = (opcode - 0x100) + 0x80
            table_address = MOVEMENT_DISPATCH_TABLE + table_index * 4
            handler = self.rom.u32(table_address) if self.rom.has(table_address, 4) else None
            result[f"0x{opcode:02X}"] = {
                "shared_animation_opcode": f"0x{self.animation_opcode(opcode):02X}",
                "handler": hex_address(handler),
            }
        return result

    def decode_stream(
        self,
        entry: int,
        max_steps: int,
        max_bytes: int,
        follow_control_flow: bool,
    ) -> dict[str, Any]:
        end = min(len(self.rom.data), entry + max_bytes)
        cursor = entry
        steps: list[dict[str, Any]] = []
        visited_steps: set[int] = set()
        stopped_reason = "step_limit"

        for _ in range(max_steps):
            if cursor in visited_steps:
                stopped_reason = "step_control_flow_cycle"
                break
            visited_steps.add(cursor)
            if not self.rom.has(cursor, 2):
                stopped_reason = "truncated_delta"
                break
            if cursor >= end:
                stopped_reason = "byte_limit"
                break

            step_start = cursor
            dx = signed_byte(self.rom.u8(cursor))
            dy = signed_byte(self.rom.u8(cursor + 1))
            cursor += 2
            commands: list[dict[str, Any]] = []
            command_stop = "next_step_delta"
            consumed_bytes = 2
            non_linear = False
            visited_commands: set[int] = set()

            while cursor < end and self.rom.has(cursor, 2):
                if cursor in visited_commands:
                    command_stop = "command_control_flow_cycle"
                    non_linear = True
                    break
                opcode = self.rom.u8(cursor)
                if not MOVEMENT_FIRST <= opcode <= MOVEMENT_LAST:
                    break

                visited_commands.add(cursor)
                command = self.decode_command(cursor, opcode)
                commands.append(command)
                command_address = cursor
                command_size = int(command["size"])
                cursor += command_size
                consumed_bytes += command_size

                shared_opcode = self.animation_opcode(opcode)
                target_text = command.get("branch_target")
                target = int(target_text, 16) if target_text else None

                if opcode == 0x80:
                    command_stop = "unconditional_jump"
                    if not follow_control_flow or target is None:
                        break
                    non_linear = True
                    cursor = target
                    continue
                if opcode == 0x92:
                    if self.rom.u8(command_address + 1) & 0x80:
                        command_stop = "dynamic_return"
                        break
                    command_stop = "dynamic_call"
                    if not follow_control_flow or target is None:
                        break
                    non_linear = True
                    cursor = target
                    continue
                if opcode == 0x8E:
                    command_stop = "dynamic_state_selection"
                    break

                # Conditional handlers expose their branch target, but the
                # taken path depends on actor/RAM state.  Continue through the
                # fall-through bytes for a deterministic static listing.
                if shared_opcode in (0xF0, 0xF4, 0xFD, 0xFE):
                    command["control_flow"] = "conditional_fallthrough"

            step_raw = None
            if not non_linear and cursor == step_start + consumed_bytes:
                step_raw = self.rom.slice(step_start, consumed_bytes).hex().upper()
            step = {
                "address": hex_address(step_start),
                "size": consumed_bytes,
                "raw": step_raw,
                "delta_x": dx,
                "delta_y": dy,
                "commands": commands,
                "next_address": hex_address(cursor),
                "command_stop": command_stop,
            }
            steps.append(step)

            if cursor <= step_start:
                if non_linear:
                    stopped_reason = "control_flow_cycle"
                else:
                    stopped_reason = "invalid_cursor_progress"
                break
        else:
            stopped_reason = "step_limit"

        return {
            "entry": hex_address(entry),
            "bytes_decoded": sum(step["size"] for step in steps),
            "steps": steps,
            "stopped_reason": stopped_reason,
        }


def parse_entry(value: str) -> tuple[str, int]:
    if "=" in value:
        name, address = value.split("=", 1)
        return name, int(address, 0)
    address = int(value, 0)
    return f"ACTOR_MOVE_STATE_{address:06X}", address


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "rom",
        nargs="?",
        type=Path,
        default=ROOT / "rom/Disneys_Aladdin_U_p1.bin",
    )
    parser.add_argument(
        "--entry",
        action="append",
        default=[],
        metavar="NAME=ADDRESS",
        help="decode an additional stream entry; may be repeated",
    )
    parser.add_argument("--max-steps", type=int, default=256)
    parser.add_argument("--max-bytes", type=lambda value: int(value, 0), default=0x4000)
    parser.add_argument("--no-follow-control-flow", action="store_true")
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "build/re/movement_streams.json",
    )
    args = parser.parse_args()

    entries = dict(DEFAULT_ENTRIES)
    for value in args.entry:
        name, address = parse_entry(value)
        entries[name] = address

    rom = load_animation_decoder().RomReader(args.rom.resolve().read_bytes())
    decoder = MovementDecoder(rom)
    streams = {}
    for name, address in sorted(entries.items(), key=lambda item: item[1]):
        decoded = decoder.decode_stream(
            address,
            args.max_steps,
            args.max_bytes,
            not args.no_follow_control_flow,
        )
        decoded["name"] = name
        streams[name] = decoded

    result = {
        "format": "openaladdin-movement-streams-v1",
        "rom": {"path": str(args.rom.resolve()), "size": args.rom.stat().st_size},
        "vm": {
            "entry_interpreter": "0x001ADE36",
            "command_range": ["0x80", "0x94"],
            "shared_animation_opcode_offset": "0x6A",
            "actor_table": "0x00FF7E40",
            "actor_stride": "0x42",
            "movement_pc_offset": "0x0A",
            "velocity_x_offset": "0x18",
            "velocity_y_offset": "0x1A",
            "dispatch_table": "0x004954",
            "dispatch": decoder.dispatch_table(),
        },
        "streams": streams,
    }
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"decoded {len(streams)} movement streams -> {output}")
    for name, stream in streams.items():
        print(
            f"{name}: {len(stream['steps'])} steps, "
            f"{stream['bytes_decoded']} bytes, stopped={stream['stopped_reason']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
