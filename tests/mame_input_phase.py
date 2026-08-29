#!/usr/bin/env python3
"""Calibrate the recorded input/state phase against a real MAME run.

This is intentionally an opt-in integration test because it needs a gameplay
save state.  Run it with a checkpoint that is already in stable gameplay:

    python3 tests/mame_input_phase.py --load-state path/to/gameplay.sta
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from genie.common import hashes
from genie.games.aladdin.mame.runs import _clean_mame_environment
from genie.games.aladdin.mame.state import load_state_trace, synchronize_state_trace
from genie.runtime import default_rom, run_shell_tool


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--load-state", type=Path)
    parser.add_argument("--right-frame", type=int, default=20)
    parser.add_argument("--settle-frames", type=int, default=20)
    args = parser.parse_args()

    state = args.load_state
    if state is None:
        candidates = [
            ROOT / "build/re/codex-upper-tower-grid-m1-n0-20260825/states/genesis/upper-platform.sta",
            ROOT / "build/runs/level01-manual/checkpoints/genesis/level01-entry.sta",
        ]
        state = next((candidate for candidate in candidates if candidate.is_file()), None)
    if state is None or not state.is_file():
        print("MAME input phase: skipped (provide a stable gameplay --load-state)")
        return 0
    if args.right_frame < 1 or args.settle_frames < 1:
        raise SystemExit("--right-frame and --settle-frames must be positive")

    frame_count = args.right_frame + args.settle_frames + 1
    schedule = f"none*{args.right_frame},right*1,none*{args.settle_frames}"
    with tempfile.TemporaryDirectory(prefix="openaladdin-input-phase-") as name:
        trace = Path(name)
        environment = _clean_mame_environment()
        environment.update({
            "OPENALADDIN_TRACE_DIR": str(trace),
            "OPENALADDIN_TRACE_FRAMES": str(frame_count - 1),
            "OPENALADDIN_INPUT": schedule,
            "OPENALADDIN_CAPTURE": "state",
            "OPENALADDIN_STATE_SYNC": "1",
            "OPENALADDIN_LOAD_STATE": str(state.resolve()),
            "OPENALADDIN_MAME_HEADLESS": "1",
            "OPENALADDIN_MAME_VIDEO": "none",
            "OPENALADDIN_MAME_SOUND": "none",
            "OPENALADDIN_EXECUTION_PROFILE": "analysis",
            "OPENALADDIN_ROM_SHA256": hashes(default_rom())["sha256"],
        })
        status = run_shell_tool(
            "mame/run.sh", [str(default_rom())], env=environment
        )
        if status != 0:
            return status
        synchronize_state_trace(trace)
        _, states, _ = load_state_trace(trace / "state.jsonl")

        assert states[args.right_frame]["input"] == "right", (
            f"I[{args.right_frame}] was not recorded at the requested input edge"
        )
        assert states[args.right_frame - 1]["input"] == "none"
        # A state at N is sampled before the transition driven by I[N]. Any
        # observed first response must therefore occur at a later boundary.
        response_frames = [
            frame for frame in sorted(states)
            if frame > args.right_frame
            if states[frame]["player"].get("vx")
            != states[args.right_frame - 1]["player"].get("vx")
        ]
        assert response_frames, "the calibration input produced no player-VX response"
        assert response_frames[0] > args.right_frame
        print(
            "MAME input phase: ok "
            f"I[{args.right_frame}] -> first PLAYER_VX response S[{response_frames[0]}]"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
