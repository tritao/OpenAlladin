#!/usr/bin/env python3
"""Regression for the native Level-01 interaction-map spawner."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/re/tests/native-interaction-spawner/state.jsonl"


def states(path: Path) -> dict[int, dict]:
    result = {}
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            value = json.loads(line)
            if value.get("type") == "state":
                result[int(value["frame"])] = value
    return result


def active_slots(state: dict) -> set[int]:
    return {
        item["slot"]
        for item in state["actors"]
        if item.get("type", 0) != 0 or item.get("flags", 0) != 0
    }


def main() -> int:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--no-audio",
        "--frames",
        "2",
        "--state-output",
        str(OUTPUT),
        # The exact Level-01 source cell (249,14), whose selector is 0x80,
        # enters the initial right-edge interaction refill window here.
        "--checkpoint-camera",
        "3632,224,3632,224,0,0,1,-1,-1,-1",
    ]
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True)

    captured = states(OUTPUT)
    assert active_slots(captured[0]) == {0}

    # Interaction refill runs after the current actor-animation traversal, so
    # the newly allocated record exposes its template root for one boundary.
    first_visible = next(actor for actor in captured[1]["actors"] if actor["slot"] == 20)
    assert first_visible["animation_pc"] == 0x00123B38
    assert first_visible["sprite_attribute"] == 0x6000

    # The following shared traversal performs the actor's first animation
    # tick, entering the stream decoded by the MAME side-effect finding.
    actor = next(actor for actor in captured[2]["actors"] if actor["slot"] == 20)
    assert actor["type"] == 0x87
    assert actor["x"] == 3952
    assert actor["y"] == 464
    assert actor["movement_flags"] == 0x21
    assert actor["flags"] == 0x04
    assert actor["animation_pc"] == 0x00123AC4
    assert actor["sprite_attribute"] == 0x6000

    print("native interaction spawner: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
