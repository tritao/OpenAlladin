"""Recorded-run replay command."""

from __future__ import annotations

import argparse
import os

from genie.runtime import *
from genie.games.aladdin.mame.experiments import *
from genie.games.aladdin.mame.state import *
from genie.games.aladdin.mame.runs import *
def command_replay(args: argparse.Namespace) -> int:
    run_dir, manifest = _load_run_manifest(args.name)
    rom = _run_rom(args, manifest)
    input_header, records = load_input_timeline(run_dir / "input.jsonl")
    frame_count = len(records)
    segment = select_segment(run_dir, args.segment) if args.segment else None
    segment_id = str(segment["id"]) if segment else None
    replay_dir = run_dir / "replay" / args.client
    if segment:
        replay_dir /= str(segment["slug"])
    replay_dir.mkdir(parents=True, exist_ok=True)

    if args.client == "mame":
        if segment:
            reference, initial_state, tokens, segment_frames = _prepare_segment_replay(
                run_dir, segment, input_header, records, replay_dir, client="mame"
            )
            mame_state_reference = str(segment.get("mame_state", ""))
            if not mame_state_reference:
                raise SystemExit(f"segment {segment_id!r} has no MAME save-state reference")
            mame_state = _event_state_path(run_dir, mame_state_reference)
            if not mame_state.is_file():
                raise SystemExit(f"segment {segment_id!r} MAME state not found: {mame_state}")
            expected_state_hash = segment.get("mame_state_sha256")
            if expected_state_hash and hashes(mame_state)["sha256"] != expected_state_hash:
                raise SystemExit(f"segment {segment_id!r} MAME state hash mismatch: {mame_state}")
            trace = replay_dir / "state.jsonl"
            # The preloaded state is captured once before emulation, leaving
            # one checkpoint sample at frame zero. The first emulated frame
            # must already consume the segment's first canonical token; the
            # checkpoint sample is removed by rebasing after MAME exits.
            mame_tokens = _mame_segment_input_tokens(
                _client_input_tokens(input_header, tokens)
            )
            environment = _clean_mame_environment()
            environment.update({
                "OPENALADDIN_TRACE_DIR": str(replay_dir),
                "OPENALADDIN_TRACE_FRAMES": str(segment_frames + 1),
                "OPENALADDIN_CAPTURE": "state",
                "OPENALADDIN_INPUT_MODE": "inject",
                "OPENALADDIN_INPUT": compress_input_schedule(mame_tokens),
                "OPENALADDIN_INPUT_OUTPUT": str(replay_dir / "mame-input.jsonl"),
                "OPENALADDIN_STATE_DIRECTORY": str(replay_dir / "checkpoints"),
                "OPENALADDIN_CHECKPOINT_REFERENCE": "checkpoints",
                "OPENALADDIN_LOAD_STATE": str(mame_state),
                "OPENALADDIN_PRELOAD_BEFORE_CAPTURE": "1",
                "OPENALADDIN_MAME_HEADLESS": "1",
                "OPENALADDIN_MAME_VIDEO": "none",
                "OPENALADDIN_ROM_SHA256": manifest["rom_sha256"],
            })
            if initial_state.get("actors"):
                environment["OPENALADDIN_TRACE_ACTORS"] = "1"
            status = run_shell_tool("mame/run.sh", [str(rom)], env=environment)
            if status == 0:
                if not reference.is_file() or not trace.is_file():
                    print("replay: segment reference or replay state trace is missing", file=sys.stderr)
                    status = 1
                else:
                    normalize_derived_state_trace(trace)
                    _rebase_state_trace_in_place(
                        trace,
                        1,
                        segment_frames + 1,
                        segment_id or "segment",
                        input_tokens=tokens,
                    )
                    status = run_tool(
                        "mame/compare_state.py",
                        [str(reference), str(trace), "--allow-additional-fields"],
                    )
            _update_replay_manifest(run_dir, manifest, args.client, trace, status, segment_id)
            if status == 0:
                print(f"replay: MAME segment {segment_id} PASS ({segment_frames + 1} state frame(s))")
            else:
                print(f"replay: MAME segment {segment_id} failed; trace {trace}", file=sys.stderr)
            return status

        mame_input = run_dir / "mame.inp"
        if not mame_input.is_file():
            raise SystemExit(f"MAME input recording not found: {mame_input}")
        trace = replay_dir / "state.jsonl"
        environment = _clean_mame_environment()
        environment.update({
            "OPENALADDIN_TRACE_DIR": str(replay_dir),
            # The recording capture includes S[0] plus the endpoint after
            # the final recorded input. Match that limit so the second MAME
            # playback has the same synchronization-qualified frame set.
            "OPENALADDIN_TRACE_FRAMES": str(max(frame_count, 0)),
            "OPENALADDIN_CAPTURE": "state",
            "OPENALADDIN_INPUT_MODE": "playback",
            "OPENALADDIN_INPUT_OUTPUT": str(replay_dir / "input.jsonl"),
            "OPENALADDIN_PLAYBACK_FILE": str(mame_input),
            "OPENALADDIN_STATE_SYNC": "1",
            "OPENALADDIN_STATE_DIRECTORY": str(replay_dir / "checkpoints"),
            "OPENALADDIN_CHECKPOINT_REFERENCE": "checkpoints",
            "OPENALADDIN_MAME_HEADLESS": "1",
            "OPENALADDIN_MAME_VIDEO": "none",
            "OPENALADDIN_ROM_SHA256": manifest["rom_sha256"],
        })
        original = run_dir / "state.jsonl"
        if original.is_file():
            _, original_states, _ = load_state_trace(original)
            if any(record.get("actors") for record in original_states.values()):
                environment["OPENALADDIN_TRACE_ACTORS"] = "1"
        _apply_run_start(environment, manifest)
        status = run_shell_tool("mame/run.sh", [str(rom)], env=environment)
        if status == 0:
            if not original.is_file() or not trace.is_file():
                print("replay: original or replay state trace is missing", file=sys.stderr)
                status = 1
            else:
                try:
                    synchronize_state_trace(replay_dir, rom_path=rom)
                except SystemExit as error:
                    print(f"replay: cannot derive synchronized state: {error}", file=sys.stderr)
                    status = 1
                else:
                    if input_header and input_header.get("controller_mapping") is None:
                        _relabel_state_trace_inputs(
                            trace,
                            input_tokens(records),
                            "legacy-full-replay",
                        )
                    status = run_tool(
                        "mame/compare_state.py",
                        [
                            str(original),
                            str(trace),
                            "--allow-additional-fields",
                            "--require-atomic",
                            "--atomic-only",
                        ],
                    )
        _update_replay_manifest(run_dir, manifest, args.client, trace, status)
        _update_trace_quality_roundtrip(
            run_dir, "pass" if status == 0 else "fail"
        )
        if status == 0:
            print(f"replay: MAME PASS ({frame_count} frame(s))")
        else:
            print(f"replay: MAME failed; trace {trace}", file=sys.stderr)
        return status

    trace = replay_dir / "state.jsonl"
    if segment:
        reference, initial_state, tokens, segment_frames = _prepare_segment_replay(
            run_dir, segment, input_header, records, replay_dir, client="native"
        )
        native_frames = segment_frames
        scheduled_tokens = (
            # Native writes the checkpoint as state frame 0, then applies one
            # scheduled token before writing each subsequent state. The
            # checkpoint token is therefore already represented by frame 0;
            # consume source frame start+1 on the first native update.
            _client_input_tokens(input_header, tokens[1:]) if tokens else []
        )
    else:
        reference = run_dir / "state.jsonl"
        initial_state = None
        native_frames = max(frame_count - 1, 0)
        scheduled_tokens = _client_input_tokens(input_header, input_tokens(records))
    native_command = [
        str(ROOT / "run.sh"),
        "--no-window",
        "--no-audio",
        "--rom", str(rom),
        "--frames", str(native_frames),
        "--state-output", str(trace),
        "--input-schedule", readable_input_schedule(scheduled_tokens),
    ]
    if segment:
        native_command.extend(native_checkpoint_arguments(
            initial_state or {},
        ))
    native_environment = os.environ.copy()
    native_environment["SDL_VIDEODRIVER"] = "dummy"
    status = subprocess.run(native_command, cwd=ROOT, env=native_environment, check=False).returncode
    _update_replay_manifest(run_dir, manifest, args.client, trace, status, segment_id)
    if status == 0:
        if input_header and input_header.get("controller_mapping") is None:
            source_tokens = input_tokens(records)
            if segment:
                source_tokens = source_tokens[int(segment["native_start_frame"]):]
            _relabel_state_trace_inputs(
                trace,
                source_tokens,
                "legacy-native-replay",
            )
        if segment:
            print(f"replay: native segment {segment_id} trace {trace}")
        else:
            print(f"replay: native trace {trace}")
    return status

__all__ = [name for name in globals() if not name.startswith("__")]
