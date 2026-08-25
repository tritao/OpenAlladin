#!/usr/bin/env python3
"""Small deterministic regression for the fixed-ROM terrain slice."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import os


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/re/tests/native-terrain-physics/state.jsonl"
SPECIAL_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/special-state.jsonl"
SPAWN_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/spawn-state.jsonl"
LANDING_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/landing-handler-state.jsonl"
HANDLER_2B_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/handler-2b-state.jsonl"
HANDLER_29_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/handler-29-state.jsonl"
HANDLER_28_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/handler-28-state.jsonl"
HANDLER_2D_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/handler-2d-state.jsonl"
HANDLER_40_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/handler-40-state.jsonl"
HANDLER_41_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/handler-41-state.jsonl"
STOP_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/stop-state.jsonl"
COLLISION_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/collision-state.jsonl"
RIGHT_PROBE_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/right-probe-state.jsonl"
CEILING_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/ceiling-state.jsonl"
CONTOUR_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/contour-state.jsonl"
FLAT_OUTPUT = ROOT / "build/re/tests/native-terrain-physics/flat-state.jsonl"


def main() -> int:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "110",
        "--state-output",
        str(OUTPUT),
        "--checkpoint-player",
        "103,416,0,0,1",
        "--input-schedule",
        "none*30,right*30,right+c,none*50",
    ]
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    subprocess.run(command, cwd=ROOT, env=environment, check=True)

    states = {}
    with OUTPUT.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            if record.get("type") == "state":
                states[record["frame"]] = record

    assert states[1]["terrain"]["behavior"] == 0x11
    assert states[31]["terrain"]["query_result"] == 0x77
    assert states[31]["terrain"]["push_right"] == 0xFF
    assert states[61]["player"]["vy"] == -0x200
    assert states[69]["player"]["vy"] == -0x20
    assert states[70]["player"]["vy"] == 0
    assert states[71]["player"]["vy"] == 0x3C
    assert any(
        record["player"]["grounded"]
        and record["player"]["y"] == 416
        and record["terrain"]["behavior"] == 0x11
        for record in states.values()
    )

    # These checkpoints select fixed level-01 cells that the generic opening
    # room route does not visit. They protect the real handler-table indices,
    # including the 0x47 surface-mode handler that was previously unreachable
    # in the native slice because its table was truncated/misaligned.
    special_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(SPECIAL_OUTPUT),
        "--checkpoint-player",
        "160,408,0,0,1",
        "--checkpoint-camera",
        "2096,328,16,464,0,0,1",
    ]
    subprocess.run(special_command, cwd=ROOT, env=environment, check=True)
    with SPECIAL_OUTPUT.open(encoding="utf-8") as stream:
        special_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    special = special_states[1]
    assert special["terrain"]["behavior"] == 0x47
    assert special["terrain"]["surface_mode"] == 1
    assert special["terrain"]["surface_latch"] == 0xFF

    # Terrain behavior 0x0A dispatches the ROM's common surface-interaction
    # handler. On a landing result it scans actor slots 3..22, copies the
    # type-0x8C template at 0x001B7E2C, and places the new actor at the
    # player's world position. The animation VM advances its stream on the
    # following frame, so frame 1 must still have a clear frame pointer.
    spawn_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "3",
        "--state-output",
        str(SPAWN_OUTPUT),
        "--actor-records",
        "/dev/null",
        "--checkpoint-player",
        "103,416,0,0,1",
        "--checkpoint-camera",
        "378,464,378,464,0,0,1",
    ]
    subprocess.run(spawn_command, cwd=ROOT, env=environment, check=True)
    with SPAWN_OUTPUT.open(encoding="utf-8") as stream:
        spawn_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    spawn = spawn_states[1]
    spawned = next(actor for actor in spawn["actors"] if actor["slot"] == 3)
    assert spawn["terrain"]["behavior"] == 0x0A
    assert spawned["type"] == 0x8C
    assert spawned["x"] == 481
    assert spawned["y"] == 880
    assert spawned["animation_pc"] == 0x00124408
    assert spawned["frame_ptr"] == 0
    advanced = next(actor for actor in spawn_states[2]["actors"] if actor["slot"] == 3)
    assert advanced["animation_pc"] == 0x0012440C
    assert advanced["frame_ptr"] != 0

    # The fixed-ROM level has no ordinary 0x30 cell, so exercise the handler
    # through the native checkpoint fixture that mirrors the MAME runtime
    # floor-table poke. This protects the direct ROM writes independently of
    # the opening-room terrain distribution.
    landing_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(LANDING_OUTPUT),
        "--checkpoint-player",
        "87,416,0,0,1",
        "--checkpoint-terrain-behavior",
        "0x30",
        "--checkpoint-camera",
        "16,464,16,464,0,0,1",
    ]
    subprocess.run(landing_command, cwd=ROOT, env=environment, check=True)
    with LANDING_OUTPUT.open(encoding="utf-8") as stream:
        landing_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    landing = landing_states[1]
    assert landing["terrain"]["behavior"] == 0x30
    assert landing["terrain"]["landing_state"] == 0xFF
    assert landing["terrain"]["horizontal_response"] == 0
    assert landing["player"]["x"] == 88
    assert landing["player"]["vy"] == -0x40
    assert landing["player"]["grounded"] is False

    # Behavior 0x2B's accepted branch requires a clear landing state. It
    # aligns local X, selects the ROM stop stream, clears response fields,
    # and enters the positive vertical state seen in the forced MAME trace.
    handler_2b_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(HANDLER_2B_OUTPUT),
        "--actor-records",
        "/dev/null",
        "--checkpoint-player",
        "162,408,0,0,0",
        "--checkpoint-terrain-behavior",
        "0x2B",
        "--checkpoint-camera",
        "2740,312,2740,312,0,0,1",
    ]
    subprocess.run(handler_2b_command, cwd=ROOT, env=environment, check=True)
    with HANDLER_2B_OUTPUT.open(encoding="utf-8") as stream:
        handler_2b_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    handler_2b = handler_2b_states[1]
    assert handler_2b["terrain"]["behavior"] == 0x2B
    assert handler_2b["terrain"]["landing_state"] == 0
    assert handler_2b["terrain"]["horizontal_response"] == 0
    assert handler_2b["terrain"]["response_active"] == 0
    assert handler_2b["player"]["x"] == 158
    assert handler_2b["player"]["y"] == 408
    assert handler_2b["player"]["vy"] == 180
    assert handler_2b["player"]["animation_pc"] == 0x0012181A

    # Behavior 0x29 launches from a clear response state. The ROM writes the
    # launch velocities, clears the vertical/timer latches, and leaves the
    # current animation stream unchanged while the landing pass keeps the
    # player grounded for this frame.
    handler_29_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(HANDLER_29_OUTPUT),
        "--actor-records",
        "/dev/null",
        "--checkpoint-player",
        "87,416,0,0,1",
        "--checkpoint-terrain-behavior",
        "0x29",
        "--checkpoint-animation",
        "0x121E46,0",
        "--checkpoint-camera",
        "16,464,16,464,0,0,1",
    ]
    subprocess.run(handler_29_command, cwd=ROOT, env=environment, check=True)
    with HANDLER_29_OUTPUT.open(encoding="utf-8") as stream:
        handler_29_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    handler_29 = handler_29_states[1]
    assert handler_29["terrain"]["behavior"] == 0x29
    assert handler_29["terrain"]["response_active"] == 0xFF
    assert handler_29["terrain"]["vertical_stop"] == 0
    assert handler_29["terrain"]["response_timer_state"] == 0
    assert handler_29["terrain"]["landing_state"] == 1
    assert handler_29["player"]["vx"] == -0x3D8
    assert handler_29["player"]["vy"] == -0x4C4
    assert handler_29["player"]["grounded"] is True
    assert handler_29["player"]["animation_pc"] == 0x00121E46

    # Behavior 0x28 aligns the player to the tile boundary, selects the ROM
    # stop stream, and arms the four-frame transition countdown. The fixture
    # seeds the animation cursor at the pre-handler VM boundary so frame 1
    # compares directly with the MAME state.
    handler_28_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(HANDLER_28_OUTPUT),
        "--actor-records",
        "/dev/null",
        "--checkpoint-player",
        "87,416,0,0,1",
        "--checkpoint-terrain-behavior",
        "0x28",
        "--checkpoint-animation",
        "0x121E46,0",
        "--checkpoint-camera",
        "16,464,16,464,0,0,1",
    ]
    subprocess.run(handler_28_command, cwd=ROOT, env=environment, check=True)
    with HANDLER_28_OUTPUT.open(encoding="utf-8") as stream:
        handler_28_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    handler_28 = handler_28_states[1]
    assert handler_28["terrain"]["behavior"] == 0x28
    assert handler_28["terrain"]["response_active"] == 0
    assert handler_28["terrain"]["response_timer_state"] == 0
    assert handler_28["terrain"]["transition_countdown"] == 4
    assert handler_28["terrain"]["landing_state"] == 1
    assert handler_28["player"]["x"] == 103
    assert handler_28["player"]["y"] == 420
    assert handler_28["player"]["vx"] == 0
    assert handler_28["player"]["vy"] == 0
    assert handler_28["player"]["grounded"] is True
    assert handler_28["player"]["animation_pc"] == 0x00121964

    # Behavior 0x2D applies the fixed bounce impulse and selects stream
    # 0x00121AD8. It does not clear the landing flag itself; the frame's
    # integrator therefore exposes the grounded state and post-step velocity
    # seen in the MAME fixture.
    handler_2d_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(HANDLER_2D_OUTPUT),
        "--actor-records",
        "/dev/null",
        "--checkpoint-player",
        "87,416,0,0,1",
        "--checkpoint-terrain-behavior",
        "0x2D",
        "--checkpoint-animation",
        "0x121E46,0",
        "--checkpoint-camera",
        "16,464,16,464,0,0,1",
    ]
    subprocess.run(handler_2d_command, cwd=ROOT, env=environment, check=True)
    with HANDLER_2D_OUTPUT.open(encoding="utf-8") as stream:
        handler_2d_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    handler_2d = handler_2d_states[1]
    assert handler_2d["terrain"]["behavior"] == 0x2D
    assert handler_2d["terrain"]["landing_state"] == 1
    assert handler_2d["player"]["x"] == 83
    assert handler_2d["player"]["y"] == 418
    assert handler_2d["player"]["vx"] == -984
    assert handler_2d["player"]["vy"] == 452
    assert handler_2d["player"]["grounded"] is True
    assert handler_2d["player"]["animation_pc"] == 0x00121AD8

    # Behaviors 0x40/0x41 are the exact eight-pixel horizontal terrain
    # corrections. Both clear FFF0B0 and leave the rest of player state alone.
    for behavior, output_path, expected_x in (
        ("0x40", HANDLER_40_OUTPUT, 95),
        ("0x41", HANDLER_41_OUTPUT, 79),
    ):
        horizontal_command = [
            str(ROOT / "build/openaladdin"),
            "--no-window",
            "--frames",
            "2",
            "--state-output",
            str(output_path),
            "--actor-records",
            "/dev/null",
            "--checkpoint-player",
            "87,416,0,0,1",
            "--checkpoint-terrain-behavior",
            behavior,
            "--checkpoint-camera",
            "16,464,16,464,0,0,1",
        ]
        subprocess.run(horizontal_command, cwd=ROOT, env=environment, check=True)
        with output_path.open(encoding="utf-8") as stream:
            horizontal_states = {
                record["frame"]: record
                for record in map(json.loads, stream)
                if record.get("type") == "state"
            }
        horizontal = horizontal_states[1]
        assert horizontal["terrain"]["behavior"] == int(behavior, 16)
        assert horizontal["terrain"]["horizontal_response"] == 0
        assert horizontal["player"]["x"] == expected_x
        assert horizontal["player"]["y"] == 416
        assert horizontal["player"]["vx"] == 0
        assert horizontal["player"]["vy"] == 0

    stop_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(STOP_OUTPUT),
        "--checkpoint-player",
        "162,408,0,180,0",
        "--checkpoint-camera",
        "2740,312,16,464,0,0,1",
    ]
    subprocess.run(stop_command, cwd=ROOT, env=environment, check=True)
    with STOP_OUTPUT.open(encoding="utf-8") as stream:
        stop_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    stop = stop_states[1]
    assert stop["terrain"]["behavior"] == 0x2B
    assert stop["player"]["vx"] == 0
    assert stop["player"]["vy"] > 0

    # The fixed-ROM collision probe sees the vertical wall at map column 141
    # without a rectangle scan or behavior-handler dispatch. Holding left at
    # its edge keeps the world position fixed. This checkpoint is deliberately
    # on the vertical wall rather than a floor contour, so the player is
    # airborne while the collision probe itself is being asserted.
    collision_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(COLLISION_OUTPUT),
        "--checkpoint-player",
        "256,408,0,0,1",
        "--checkpoint-camera",
        "2000,312,2000,312,0,0,1",
        "--input-schedule",
        "left*2",
    ]
    subprocess.run(collision_command, cwd=ROOT, env=environment, check=True)
    with COLLISION_OUTPUT.open(encoding="utf-8") as stream:
        collision_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    collision = collision_states[1]
    assert collision["player"]["world_x"] == 2256
    assert collision["terrain"]["stop_left_motion"] == 0xFF
    assert collision["terrain"]["stop_right_motion"] == 0

    # The ROM's right-side probe starts two columns to the right of the
    # left-side base, then records inner/outer probes at +3/+4. This fixed
    # map checkpoint has a blocking cell at +4 only; the old +1 mirror
    # silently missed it.
    right_probe_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(RIGHT_PROBE_OUTPUT),
        "--checkpoint-player",
        "144,320,0,0,0",
        "--checkpoint-camera",
        "2000,400,2000,400,0,0,1",
    ]
    subprocess.run(right_probe_command, cwd=ROOT, env=environment, check=True)
    with RIGHT_PROBE_OUTPUT.open(encoding="utf-8") as stream:
        right_probe_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    right_probe = right_probe_states[1]
    assert right_probe["player"]["world_x"] == 2144
    assert right_probe["terrain"]["right_inner_probe"] == 0
    assert right_probe["terrain"]["right_outer_probe"] == 0xFF
    assert right_probe["terrain"]["stop_right_motion"] == 0

    # A blocking cell directly above the probe stops negative VY in the
    # integrator, before terrain-handler resolution runs.
    ceiling_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(CEILING_OUTPUT),
        "--checkpoint-player",
        "192,360,0,-512,0",
        "--checkpoint-camera",
        "2000,400,2000,400,0,0,1",
    ]
    subprocess.run(ceiling_command, cwd=ROOT, env=environment, check=True)
    with CEILING_OUTPUT.open(encoding="utf-8") as stream:
        ceiling_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    ceiling = ceiling_states[1]
    assert ceiling["player"]["vy"] == 0
    assert ceiling["terrain"]["stop_upward_motion"] == 0xFF
    assert ceiling["terrain"]["vertical_stop"] == 0xFF

    # The contour table is indexed by the raw floor type and horizontal
    # sub-tile fraction. At this checkpoint the original routine selects a
    # non-flat contour and places the player one pixel above the row base.
    contour_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(CONTOUR_OUTPUT),
        "--checkpoint-player",
        "152,416,0,0,1",
        "--checkpoint-camera",
        "1000,464,1000,464,0,0,1",
    ]
    subprocess.run(contour_command, cwd=ROOT, env=environment, check=True)
    with CONTOUR_OUTPUT.open(encoding="utf-8") as stream:
        contour_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    contour = contour_states[1]
    assert contour["player"]["world_y"] == 879
    assert contour["player"]["y"] == 415
    assert contour["player"]["grounded"] is True
    assert contour["terrain"]["landing_state"] == 16

    # The same lookup must retain the ordinary flat-ground result: the next
    # row contains raw floor type 1, whose contour byte is 1 at every X
    # fraction, producing the unchanged world-Y target 912.
    flat_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(FLAT_OUTPUT),
        "--checkpoint-player",
        "104,416,0,0,1",
        "--checkpoint-camera",
        "2000,496,2000,496,0,0,1",
    ]
    subprocess.run(flat_command, cwd=ROOT, env=environment, check=True)
    with FLAT_OUTPUT.open(encoding="utf-8") as stream:
        flat_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    flat = flat_states[1]
    assert flat["player"]["world_y"] == 912
    assert flat["player"]["y"] == 416
    assert flat["player"]["grounded"] is True
    assert flat["terrain"]["landing_state"] == 1

    # MAME's first-level type-0x1F actor raises flag bit 5 when the player
    # reaches world X=0x2A4 at the ground line. The selector then arms the
    # seven-frame camera delay; update_camera consumes one count immediately.
    gate_output = ROOT / "build/re/tests/native-terrain-physics/gate-state.jsonl"
    gate_command = [
        str(ROOT / "build/openaladdin"),
        "--no-window",
        "--frames",
        "2",
        "--state-output",
        str(gate_output),
        "--checkpoint-player",
        "148,416,0,0,1",
        "--checkpoint-camera",
        "528,464,528,464,0,0,1",
        "--input-schedule",
        "right*2",
    ]
    subprocess.run(gate_command, cwd=ROOT, env=environment, check=True)
    with gate_output.open(encoding="utf-8") as stream:
        gate_states = {
            record["frame"]: record
            for record in map(json.loads, stream)
            if record.get("type") == "state"
        }
    gate = gate_states[1]
    assert gate["player"]["x"] == 151
    assert gate["camera"]["x"] == 528
    assert gate["camera"]["update_delay"] == 6
    actor4 = next(actor for actor in gate["actors"] if actor["slot"] == 4)
    assert actor4["type"] == 0x1F
    assert actor4["flags"] == 0x20
    assert actor4["flag_bit5"] is True

    print("native terrain physics: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
