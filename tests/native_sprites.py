#!/usr/bin/env python3
"""Smoke-test the native player animation VM and Chopper frame database."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def run_case(
    name: str,
    checkpoint: str,
    schedule: str,
    frames: int = 2,
    animation: str | None = None,
    animation_selector: str | None = None,
) -> list[dict]:
    output = ROOT / "build/re/tests/native-sprites" / f"{name}.jsonl"
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        str(frames),
        "--state-output",
        str(output),
        "--checkpoint-player",
        checkpoint,
        "--input-schedule",
        schedule,
    ]
    if animation is not None:
        command.extend(["--checkpoint-animation", animation])
    if animation_selector is not None:
        command.extend(["--checkpoint-animation-selector", animation_selector])
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True, stdout=subprocess.DEVNULL)
    with output.open(encoding="utf-8") as stream:
        return [
            record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        ]


def main() -> int:
    idle = run_case("idle", "103,416,0,0,1", "none*2")
    assert idle[0]["player"]["sprite_frame"] == 201

    running = run_case("run", "103,416,0,0,1", "right*7", frames=7)
    run_frames = [record["player"]["sprite_frame"] for record in running]
    assert run_frames[:5] == [201, 201, 201, 201, 201]
    assert 202 in run_frames
    assert running[-1]["player"]["animation_state"] == "run"

    reversal = run_case("reversal", "103,416,0,0,1", "left*4,right*4", frames=8)
    assert [record["player"]["facing_x_flip"] for record in reversal] == [
        0, 255, 255, 255, 255, 0, 0, 0, 0
    ]

    jumping = run_case("jump", "103,416,0,-512,0", "none*7", frames=7)
    jump_frames = [record["player"]["sprite_frame"] for record in jumping]
    assert jump_frames[:5] == [161, 161, 161, 161, 162]
    assert jumping[-1]["player"]["sprite_frame"] == 162
    assert jumping[-1]["player"]["animation_state"] == "jump"

    # A response stream is gameplay-owned state, not an alternate idle pose.
    # With its selector gates inactive it must hand control back to
    # locomotion immediately instead of leaving the hurt-looking stream
    # running because the player is grounded and has no input.
    recovered = run_case(
        "response-recovery",
        "103,416,0,0,1",
        "none*2",
        frames=2,
        animation="0x00121FA6,0",
    )
    assert recovered[1]["player"]["animation_state"] == "idle"
    assert recovered[1]["player"]["animation_stream_entry"] == 0x00121D9A
    assert recovered[1]["player"]["frame_ptr"] == 0x001EA34A

    recovered_inside = run_case(
        "response-recovery-inside",
        "103,416,0,0,1",
        "none*2",
        frames=2,
        animation="0x00121FA8,0",
    )
    assert recovered_inside[1]["player"]["animation_state"] == "idle"
    assert recovered_inside[1]["player"]["animation_stream_entry"] == 0x00121D9A

    # The same response stream remains response-owned while its selector
    # gates are active. This guards the opposite failure mode: an ordinary
    # locomotion update must not overwrite a live hurt response with idle.
    active = run_case(
        "response-active",
        "103,416,0,0,1",
        "none*2",
        frames=2,
        animation="0x00121FA6,0",
        animation_selector="0,0,0,0,255,1,0,0,0,0,0,0,0,0,0,0,255,0,0,0,0,0,0,0,0",
    )
    assert active[1]["player"]["animation_state"] == "response"
    assert active[1]["player"]["animation_stream_entry"] == 0x00121FA6

    active_inside = run_case(
        "response-active-inside",
        "103,416,0,0,1",
        "none*2",
        frames=2,
        animation="0x00121FA8,0",
        animation_selector="0,0,0,0,255,1,0,0,0,0,0,0,0,0,0,0,255,0,0,0,0,0,0,0,0",
    )
    assert active_inside[1]["player"]["animation_state"] == "response"
    assert active_inside[1]["player"]["animation_stream_entry"] == 0x00121FA6

    print("native sprites: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
