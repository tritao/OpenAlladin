#!/usr/bin/env python3
"""Tests for semantic event loading and derived recording segments."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import tools.oa as oa


def main() -> int:
    protocol = oa.event_detector_protocol()
    assert protocol is not None
    assert "level01-entry" in protocol
    assert "scene-state-08" in protocol
    assert "|once|" in protocol
    assert oa._client_input_tokens(None, ["a+b", "c", "right"]) == [
        "b+c", "a", "right"
    ]
    assert oa._client_input_tokens(
        {"controller_mapping": oa.INPUT_MAPPING}, ["a+b", "c"]
    ) == ["a+b", "c"]

    animation_states = {
        0: {"player": {"animation_pc": 0x122028, "frame_ptr": 0x1EA3C2}},
        1: {"player": {"animation_pc": 0x122028, "frame_ptr": 0x1EA410}},
        2: {"player": {"animation_pc": 0x12202A, "frame_ptr": 0x1EA410}},
    }
    assert oa._normalize_animation_write_order(animation_states) == 1
    assert animation_states[1]["player"]["animation_pc"] == 0x12202A

    timer_states = {
        0: {"player": {"animation_pc": 0x12227C, "frame_ptr": 0x1E9F24, "animation_timer": 3}},
        1: {"player": {"animation_pc": 0x12227C, "frame_ptr": 0x1E9F7E, "animation_timer": 3}},
        2: {"player": {"animation_pc": 0x12227C, "frame_ptr": 0x1E9F7E, "animation_timer": 2}},
    }
    assert oa._normalize_animation_write_order(timer_states) == 1
    assert timer_states[1]["player"]["animation_timer"] == 2

    duplicate_frame_states = {
        0: {"player": {"animation_pc": 0x122026, "frame_ptr": 0x1EA3C2}},
        1: {"player": {"animation_pc": 0x122028, "frame_ptr": 0x1EA3C2}},
        2: {"player": {"animation_pc": 0x12202A, "frame_ptr": 0x1EA410}},
    }
    assert oa._normalize_animation_write_order(duplicate_frame_states) == 0

    with tempfile.TemporaryDirectory(prefix="openaladdin-events-test-") as name:
        run_dir = Path(name)
        checkpoint = run_dir / "checkpoints/genesis/level01-entry.sta"
        checkpoint.parent.mkdir(parents=True)
        checkpoint.write_bytes(b"checkpoint")
        (run_dir / "events.jsonl").write_text(
            "\n".join([
                json.dumps({"type": "header", "format": oa.EVENT_FORMAT}),
                json.dumps({
                    "type": "event",
                    "frame": 12,
                    "name": "level01-entry",
                    "event": "level_entry",
                    "phase": "gameplay",
                    "level": "level01",
                    "checkpoint": "level01-entry",
                    "state": "checkpoints/genesis/level01-entry.sta",
                }),
            ]) + "\n",
            encoding="utf-8",
        )
        state_header = {"type": "header", "format": "openaladdin-frame-state-v1"}
        state_records = [state_header]
        for frame in range(30):
            state_records.append({
                "type": "state",
                "frame": frame,
                "input": "right" if frame >= 12 else "none",
                "player": {
                    "x": 100 + frame,
                    "y": 200,
                    # Simulate the transition's stale world coordinates until
                    # frame 15; readiness must discover the first stable
                    # native boundary instead of assuming event+N.
                    "world_x": 0 if frame < 15 else 10 + 100 + frame,
                    "world_y": 0 if frame < 15 else 20 + 200,
                    "vx": 1,
                    "vy": 0,
                    "grounded": True,
                    "frame_ptr": 0x123456,
                    "animation_pc": 0 if frame < 1 else (0x12542A if frame < 18 else 0x12542C),
                    "animation_timer": 2,
                    "facing_x_flip": 0,
                },
                "scene": {"state": 1},
                "camera": {
                    "x": 10,
                    "y": 20,
                    "reference_x": 10,
                    "reference_y": 20,
                    "scroll_x": 0,
                    "scroll_y": 0,
                },
                "terrain": {"behavior": 0x10, "landing_state": 1},
            })
        (run_dir / "state.jsonl").write_text(
            "\n".join(json.dumps(record) for record in state_records) + "\n",
            encoding="utf-8",
        )
        raw_bytes = (run_dir / "state.jsonl").read_bytes()
        semantic = run_dir / "state.semantic.jsonl"
        assert oa.normalize_animation_state_trace(run_dir / "state.jsonl", semantic) == 0
        assert (run_dir / "state.jsonl").read_bytes() == raw_bytes
        derived_header, derived_states, _ = oa.load_state_trace(semantic)
        assert derived_states[0]["frame"] == 0
        assert derived_header["source_artifact"] == "state.jsonl"
        input_records = [
            {"type": "header", "format": oa.INPUT_FORMAT},
            *[
                {"frame": frame, "mask": 8 if frame >= 12 else 0, "buttons": ["right"] if frame >= 12 else []}
                for frame in range(30)
            ],
        ]
        (run_dir / "input.jsonl").write_text(
            "\n".join(json.dumps(record) for record in input_records) + "\n",
            encoding="utf-8",
        )

        event_count, segment_count = oa._write_segments(run_dir, 30)
        assert (event_count, segment_count) == (1, 1)
        document = json.loads((run_dir / "segments.json").read_text(encoding="utf-8"))
        segment = document["segments"][0]
        assert segment["event_frame"] == 12
        assert segment["start_frame"] == 13
        assert segment["end_frame"] == 29
        assert segment["mame_state_sha256"] == oa.hashes(checkpoint)["sha256"]
        assert segment["native_start_frame"] == 15
        assert segment["native_ready"]["status"] == "ready"
        assert segment["native_start"]["player"]["x"] == 115
        assert segment["native_animation_phase"]["status"] == "inferred"
        assert segment["native_animation_phase"]["delay_ticks"] == 1
        assert segment["native_start"]["terrain"]["behavior"] == 0x10
        assert segment["native_start"]["terrain"]["landing_state"] == 1

        segments = oa.load_segments(run_dir)
        selected = oa.select_segment(run_dir, "level01-entry")
        assert segments[0]["id"] == selected["id"]
        reference = run_dir / "replay/native/level01-entry/genesis.jsonl"
        initial = oa._write_sliced_state(run_dir / "state.jsonl", reference, 13, 15, "level01-entry")
        assert initial["frame"] == 13
        _, sliced_states, _ = oa.load_state_trace(reference)
        assert sorted(sliced_states) == [0, 1, 2]
        assert sliced_states[0]["player"]["x"] == 113
        arguments = oa.native_checkpoint_arguments(initial)
        assert "--checkpoint-terrain-behavior" in arguments
        assert "--checkpoint-terrain-landing-state" in arguments
        assert "--checkpoint-player" in arguments
        arguments = oa.native_checkpoint_arguments(initial, animation_phase_delay=1)
        assert "--checkpoint-animation-phase-delay" in arguments
        rebased = run_dir / "replay/mame/level01-entry/state.jsonl"
        oa._write_sliced_state(
            run_dir / "state.jsonl",
            rebased,
            13,
            15,
            "level01-entry",
            input_tokens=["left", "right", "none"],
        )
        _, rebased_states, _ = oa.load_state_trace(rebased)
        assert [rebased_states[index]["input"] for index in range(3)] == [
            "left", "right", "none"
        ]
        # A loaded MAME checkpoint has one discarded pre-emulation sample; the
        # first real emulated frame must consume the first segment token.
        assert oa._mame_segment_input_tokens(["right", "none"]) == ["right", "none"]

        derived_events = run_dir / "derived-events.jsonl"
        events = oa.derive_events_from_state(run_dir / "state.jsonl", derived_events)
        assert [(event["onset_frame"], event["confirmed_frame"]) for event in events] == [(1, 3)]

    print("MAME semantic events: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
