#!/usr/bin/env python3
"""Regression for the recovered level-01 vertical connector response."""

from __future__ import annotations

import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "rom/Disneys_Aladdin_U_p1.bin"
BINARY = ROOT / "build/openaladdin"
OUTPUT = ROOT / "build/re/tests/native-connector-response/state.jsonl"


def run_fixture(behavior: str) -> list[dict]:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            str(BINARY),
            "--no-window",
            "--rom",
            str(ROM),
            "--frames",
            "10",
            "--state-output",
            str(OUTPUT),
            "--checkpoint-player",
            "284,360,0,0,0",
            "--checkpoint-camera",
            "4444,448,4444,448,0,0,1,176,368,0",
            "--checkpoint-terrain-behavior",
            behavior,
            "--input-schedule",
            "up*10",
            "--actor-records",
            "/dev/null",
        ],
        cwd=ROOT,
        check=True,
    )
    return [
        json.loads(line)
        for line in OUTPUT.read_text().splitlines()
        if json.loads(line).get("type") == "state"
    ]


def main() -> None:
    states = run_fixture("0x22")
    assert states[1]["terrain"]["query_state_a"] == 0xFF
    assert states[1]["player"]["world_y"] == 808
    assert states[1]["camera"]["vertical_threshold"] == 368

    # FUN_001A986E consumes the previous frame's query latch. The ROM's
    # camera pass then follows the one/two-pixel local-Y step.
    assert (states[2]["player"]["y"], states[2]["player"]["world_y"]) == (364, 806)
    assert states[2]["camera"]["vertical_threshold"] == 400
    assert states[2]["scene"]["player_gate"] == 0xFF

    # The scroll accumulator crosses -0x10 here. The rebase is visible, but
    # the ROM does not run a second follow step on that same frame.
    assert states[6]["camera"]["reference_y"] == 432
    assert (states[6]["player"]["y"], states[6]["camera"]["y"]) == (371, 429)

    stopped = run_fixture("0x24")
    assert stopped[1]["terrain"]["query_state_a"] == 0xFF
    assert stopped[1]["terrain"]["query_state_b"] == 0xFF
    assert stopped[2]["player"]["world_y"] == 808

    print("native connector response: ok")


if __name__ == "__main__":
    main()
