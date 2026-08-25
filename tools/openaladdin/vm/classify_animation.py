#!/usr/bin/env python3
"""Classify discovered animation roots by their 68K selection context.

The decoder can find stream-shaped data, but the surrounding code tells us
which state machine owns an entry.  This report looks for writes to the
confirmed player animation cursor (FF7E60), records the selecting functions,
and keeps non-player roots provisional until an actor-slot trace identifies
their concrete type.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import struct
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
PLAYER_ANIMATION_PC = 0xFF7E60
PLAYER_ANIMATION_SELECTOR = 0x001AD150


def load_decoder():
    path = ROOT / "tools/openaladdin/vm/animation.py"
    spec = importlib.util.spec_from_file_location("openaladdin_animation_decoder", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load decoder: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_u16(data: bytes, address: int) -> int:
    return struct.unpack_from(">H", data, address)[0]


def read_u32(data: bytes, address: int) -> int:
    return struct.unpack_from(">I", data, address)[0]


def function_for(functions: list[dict[str, Any]], address: int) -> dict[str, Any] | None:
    selected = None
    for function in functions:
        if int(function["address"], 16) > address:
            break
        selected = function
    return selected


def direct_player_assignment(data: bytes, pointer_address: int) -> dict[str, Any] | None:
    """Return evidence for a direct or register-mediated FF7E60 assignment."""
    if pointer_address >= 2 and read_u16(data, pointer_address - 2) == 0x23FC:
        destination = read_u32(data, pointer_address + 4)
        if destination == PLAYER_ANIMATION_PC:
            return {
                "kind": "move_immediate_to_player_animation_pc",
                "instruction": f"0x{pointer_address - 2:08X}",
            }

    # MOVE.L #stream,Dn followed shortly by MOVE.L Dn,$FF7E60.  This is used
    # by the player state selector around 1ADAxx and 1A98xx.
    if pointer_address < 2:
        return None
    opcode = read_u16(data, pointer_address - 2)
    if opcode & 0xF1FF not in (0x203C, 0x207C):
        return None

    search_start = pointer_address + 4
    search_end = min(len(data) - 6, pointer_address + 0x40)
    for address in range(search_start, search_end, 2):
        move_opcode = read_u16(data, address)
        if move_opcode & 0xFFF8 != 0x23C0:
            continue
        if read_u32(data, address + 2) == PLAYER_ANIMATION_PC:
            return {
                "kind": "move_immediate_then_register_to_player_animation_pc",
                "instruction": f"0x{pointer_address - 2:08X}",
                "assignment": f"0x{address:08X}",
            }
    return None


def classify(
    data: bytes,
    candidates: list[dict[str, Any]],
    functions: list[dict[str, Any]],
    known_player_entries: set[int],
    known_player_names: dict[int, str],
) -> list[dict[str, Any]]:
    result = []
    for candidate in candidates:
        entry = int(candidate["entry"], 16)
        references = []
        player_evidence = []
        player_selector_evidence = []
        selecting_functions = {}

        for reference in candidate["references"]:
            pointer_address = int(reference["address"], 16)
            instruction_address = int(reference["instruction"], 16)
            function = function_for(functions, instruction_address)
            evidence = direct_player_assignment(data, pointer_address)
            row = dict(reference)
            if function is not None:
                row["function"] = function["name"]
                row["function_address"] = function["address"]
                selecting_functions[function["name"]] = function["address"]
                if int(function["address"], 16) == PLAYER_ANIMATION_SELECTOR:
                    row["player_evidence"] = {
                        "kind": "player_animation_selector_returns_stream",
                        "function": function["address"],
                        "caller_assignments": f"0x{PLAYER_ANIMATION_PC:08X}",
                    }
                    player_selector_evidence.append(row)
            if evidence is not None:
                row["player_evidence"] = evidence
                player_evidence.append(row)
            references.append(row)

        is_player = (
            entry in known_player_entries
            or bool(player_evidence)
            or bool(player_selector_evidence)
        )
        if is_player:
            classification = "player_animation"
            confidence = "confirmed"
            name = f"PLAYER_ANIM_STATE_{entry:06X}"
            if entry in known_player_entries:
                name = known_player_names[entry]
            evidence = []
            if player_evidence:
                evidence.extend(
                    [
                        "player_animation_pc_assignment",
                        *[
                            f"callsite_{row['instruction']}"
                            for row in player_evidence
                        ],
                    ]
                )
            evidence.extend(
                f"player_selector_{row['instruction']}"
                for row in player_selector_evidence
            )
            if not evidence:
                evidence.append("known_player_stream")
        else:
            classification = "common_actor_animation"
            confidence = "provisional"
            name = f"ACTOR_ANIM_STATE_{entry:06X}"
            evidence = [
                "common_animation_helper_reference",
                *[
                    f"callsite_{row['instruction']}"
                    for row in references
                ],
            ]

        result.append(
            {
                "name": name,
                "entry": candidate["entry"],
                "classification": classification,
                "confidence": confidence,
                "evidence": evidence,
                "selecting_functions": selecting_functions,
                "references": references,
                "probe": candidate["probe"],
            }
        )
    return sorted(result, key=lambda item: int(item["entry"], 16))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "rom",
        nargs="?",
        type=Path,
        default=ROOT / "rom/Disneys_Aladdin_U_p1.bin",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "build/re/animation_streams_classified.json",
    )
    parser.add_argument(
        "--stream-region",
        type=lambda value: tuple(int(part, 0) for part in value.split(":", 1)),
        default=(0x00120000, 0x00130000),
        metavar="START:END",
    )
    parser.add_argument(
        "--frame-region",
        type=lambda value: tuple(int(part, 0) for part in value.split(":", 1)),
        default=(0x001E0000, 0x00200000),
        metavar="START:END",
    )
    return parser.parse_args()


def main() -> int:
    module = load_decoder()
    args = parse_args()
    data = args.rom.read_bytes()
    rom = module.RomReader(data)
    decoder = module.AnimationDecoder(rom)
    candidates = module.discover_stream_entries(
        rom,
        decoder,
        args.stream_region,
        args.frame_region,
    )
    functions_path = ROOT / "build/re/functions.json"
    functions = json.loads(functions_path.read_text()) if functions_path.exists() else []
    known_player_names = {
        address: name for name, address in module.PLAYER_STREAMS.items()
    }
    classified = classify(
        data,
        candidates,
        functions,
        set(known_player_names),
        known_player_names,
    )
    output = {
        "rom": {
            "path": str(args.rom),
            "size": len(data),
        },
        "classification": {
            "player_animation_pc": f"0x{PLAYER_ANIMATION_PC:06X}",
            "stream_region": [f"0x{value:06X}" for value in args.stream_region],
            "frame_region": [f"0x{value:06X}" for value in args.frame_region],
            "counts": {
                "total": len(classified),
                "player_animation": sum(
                    item["classification"] == "player_animation" for item in classified
                ),
                "common_actor_animation": sum(
                    item["classification"] == "common_actor_animation"
                    for item in classified
                ),
            },
        },
        "streams": classified,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(
        f"classified {len(classified)} streams "
        f"({output['classification']['counts']['player_animation']} player, "
        f"{output['classification']['counts']['common_actor_animation']} common actor) "
        f"-> {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
