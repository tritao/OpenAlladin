#!/usr/bin/env python3
"""Regression checks for the statically recovered ROM scheduler model."""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from genie.common import load_yaml, parse_int  # noqa: E402


EXPECTED_ENTRIES = [
    0x001A91C6,
    0x001A8E0C,
    0x001AD7B4,
    0x001A8E0C,
    0x001AD632,
    0x001A986E,
    0x001A99F0,
    0x001ADE36,
    0x001ADB5C,
    0x001ABB40,
    0x001B321C,
    0x001B3212,
    0x001B3226,
    0x001B3230,
    0x001B1E38,
    0x001A9D98,
    0x001A9716,
    0x001A8E0C,
    0x001AA8FA,
    0x001A9304,
    0x001A9502,
    0x001ABD7E,
    0x001B02EC,
    0x001A8F0C,
    0x001A8F04,
    0x001B00CA,
    0x001B01AC,
    0x001A8E0C,
    0x001A8E3E,
    0x001AC784,
    0x001AB7C4,
    0x001B249E,
    0x001AC726,
    0x001AB776,
    0x001AE0F6,
    0x001AAA2A,
    0x001B315C,
]


def main() -> int:
    model = load_yaml(ROOT / "re/scheduler/frame_phases.yml")
    assert model["format"] == "openaladdin-scheduler-model-v1"
    assert model["call_sequence_caller"] == "Game_FrameUpdateLoop"
    assert parse_int(model["call_sequence_caller_entry"]) == 0x001A8C16
    calls = model["call_sequence"]
    assert len(calls) == 37
    assert [parse_int(call["entry"]) for call in calls] == EXPECTED_ENTRIES
    lua_source = (ROOT / "re/mame/lua/scheduler.lua").read_text(encoding="utf-8")
    lua_calls = [
        (int(ordinal), int(call_site, 16), int(entry, 16))
        for ordinal, call_site, entry in re.findall(
            r"\{\s*(\d+),\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+)\s*\}",
            lua_source,
        )
    ]
    yaml_calls = [
        (int(call["ordinal"]), parse_int(call["call_site"]), parse_int(call["entry"]))
        for call in calls
    ]
    assert lua_calls == yaml_calls
    assert model["entry_points"]["gameplay"]["direct_call_count"] == len(calls)
    animation_calls = [call for call in calls if parse_int(call["entry"]) == 0x001AC784]
    assert [call["ordinal"] for call in animation_calls] == [30]
    assert model["entry_points"]["vblank"]["relation"].startswith("interrupt service")
    frame_wait = model["ram_gates"][1]
    assert frame_wait["symbol"] == "FRAME_WAIT_LATCH"
    assert [parse_int(writer["address"]) for writer in frame_wait["writers"]] == [
        0x001AA3A8,
        0x001B2DF4,
        0x001B2E02,
    ]
    assert model["dynamic_validation"]["runtime_latches"]["FRAME_WAIT_LATCH"]["init_owner"] == "FUN_001AA344"
    assert model["dynamic_validation"]["runtime_latches"]["FRAME_WAIT_LATCH"]["helper_role"].startswith(
        "optional Z80 handshake"
    )
    rom = (ROOT / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    assert rom[0x001AA3A8 : 0x001AA3AE] == bytes.fromhex("423900ff7e25")
    assert rom[0x001B2DF4 : 0x001B2DFA] == bytes.fromhex("50f900ff7e25")
    assert rom[0x001B2E02 : 0x001B2E08] == bytes.fromhex("423900ff7e25")
    assert rom[0x001B24A4 : 0x001B24AA] == bytes.fromhex("4a3900ff7e25")
    assert model["native_mapping"]["status"] == "resolved"
    mapping = load_yaml(ROOT / "re/scheduler/native_update_mapping.yml")
    rows = mapping["rom_rows"]
    assert [row["ordinal"] for row in rows] == list(range(1, 38))
    assert all(row["status"] in {"exact", "inlined", "derived_presentation_only"} for row in rows)
    assert mapping["summary"]["mismatch_rom_rows"] == 0
    assert mapping["summary"]["unknown_rom_rows"] == 0
    assert mapping["summary"]["native_extra_mismatch_phases"] == 0
    assert all(item["status"] == "resolved" for item in model["native_mapping"]["closed_mismatches"])
    print("scheduler model: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
