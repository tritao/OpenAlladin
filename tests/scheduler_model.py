#!/usr/bin/env python3
"""Regression checks for the statically recovered ROM scheduler model."""

from __future__ import annotations

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from openaladdin.common import load_yaml, parse_int  # noqa: E402


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
    assert model["entry_points"]["gameplay"]["direct_call_count"] == len(calls)
    animation_calls = [call for call in calls if parse_int(call["entry"]) == 0x001AC784]
    assert [call["ordinal"] for call in animation_calls] == [30]
    assert model["entry_points"]["vblank"]["relation"].startswith("interrupt service")
    assert model["native_mapping"]["status"] == "provisional"
    print("scheduler model: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
