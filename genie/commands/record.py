"""MAME recording and interactive-session commands."""

from __future__ import annotations

import argparse
import os

from genie.runtime import *
from genie.mame.experiments import *
from genie.mame.state import *
from genie.mame.runs import *
def command_record(args: argparse.Namespace) -> int:
    rom = resolve(args.rom)
    if not rom.is_file():
        raise SystemExit(f"ROM not found: {rom}")
    run_dir = run_directory(args.name)
    if run_dir.exists():
        raise SystemExit(f"run directory already exists: {run_dir}")
    if args.frames is not None and args.frames < 1:
        raise SystemExit("--frames must be positive when supplied")

    run_dir.mkdir(parents=True)
    (run_dir / "checkpoints").mkdir()
    raw_dir = run_dir / "raw"
    raw_dir.mkdir()
    manifest = _new_run_manifest(args, run_dir, rom)
    manifest["recording_pipeline"] = "interactive-input-then-analysis-capture-v1"
    manifest["recording"] = {
        "execution_profile": "interactive",
        "trace": "trace_boot.recording.jsonl",
        "input": "input.jsonl",
        "mame_input": "mame.inp",
    }
    _write_json(run_dir / "run.json", manifest)

    environment = _clean_mame_environment()
    frame_limit = -1 if args.frames is None else args.frames - 1
    environment.update({
        "OPENALADDIN_TRACE_DIR": str(run_dir),
        "OPENALADDIN_TRACE_FRAMES": str(frame_limit),
        "OPENALADDIN_CAPTURE": "none",
        "OPENALADDIN_INPUT_MODE": "record",
        "OPENALADDIN_INPUT_OUTPUT": str(run_dir / "input.jsonl"),
        "OPENALADDIN_RECORD_FILE": str(run_dir / "mame.inp"),
        "OPENALADDIN_MAME_HEADLESS": "0",
        "OPENALADDIN_MAME_VIDEO": "soft",
        "OPENALADDIN_EXECUTION_PROFILE": "interactive",
        "OPENALADDIN_MAME_SOUND": "sdl",
        "OPENALADDIN_ROM_SHA256": manifest["rom_sha256"],
    })
    if args.load_state:
        environment["OPENALADDIN_LOAD_STATE"] = str(resolve(Path(args.load_state)))

    print(f"record: interactive MAME session for {args.name}")
    print(f"record: quit MAME when the run is complete; output will be {run_dir}")
    recording_status = run_shell_tool("mame/run.sh", [str(rom)], env=environment)
    if recording_status != 0:
        final_status, valid = _finish_record_manifest(run_dir, manifest, recording_status)
    else:
        _copy_artifact(run_dir / "trace_boot.jsonl", run_dir / "trace_boot.recording.jsonl")
        try:
            _, input_records = load_input_timeline(run_dir / "input.jsonl")
        except SystemExit as error:
            print(f"record: cannot start analysis capture: {error}", file=sys.stderr)
            final_status, valid = _finish_record_manifest(run_dir, manifest, 1)
        else:
            mame_input = run_dir / "mame.inp"
            if not mame_input.is_file():
                print(f"record: missing MAME input recording {mame_input}", file=sys.stderr)
                final_status, valid = _finish_record_manifest(run_dir, manifest, 1)
            else:
                capture_environment = _clean_mame_environment()
                capture_environment.update({
                    "OPENALADDIN_TRACE_DIR": str(raw_dir),
                    # Capture S[0] through S[N] so I[N] has an explicit
                    # destination state even though the final state is not
                    # needed by the native segment replayer.
                    "OPENALADDIN_TRACE_FRAMES": str(len(input_records)),
                    "OPENALADDIN_CAPTURE": "state",
                    "OPENALADDIN_INPUT_MODE": "playback",
                    "OPENALADDIN_INPUT_OUTPUT": str(raw_dir / "input.jsonl"),
                    "OPENALADDIN_PLAYBACK_FILE": str(mame_input),
                    "OPENALADDIN_STATE_SYNC": "1",
                    "OPENALADDIN_STATE_DIRECTORY": str(run_dir / "checkpoints"),
                    "OPENALADDIN_CHECKPOINT_REFERENCE": "checkpoints",
                    "OPENALADDIN_MAME_HEADLESS": "1",
                    "OPENALADDIN_MAME_VIDEO": "none",
                    "OPENALADDIN_MAME_SOUND": "none",
                    "OPENALADDIN_EXECUTION_PROFILE": "analysis",
                    "OPENALADDIN_ROM_SHA256": manifest["rom_sha256"],
                })
                if args.load_state:
                    capture_environment["OPENALADDIN_LOAD_STATE"] = str(resolve(Path(args.load_state)))
                if args.checkpoints:
                    capture_environment["OPENALADDIN_CHECKPOINTS"] = args.checkpoints
                print("record: deterministic analysis capture from mame.inp")
                capture_status = run_shell_tool(
                    "mame/run.sh", [str(rom)], env=capture_environment
                )
                if capture_status != 0:
                    final_status, valid = _finish_record_manifest(
                        run_dir, manifest, capture_status
                    )
                else:
                    try:
                        sync_count = _materialize_record_capture(
                            run_dir, raw_dir, rom_path=rom
                        )
                        manifest["capture"] = {
                            "status": "complete",
                            "execution_profile": "analysis",
                            "raw_directory": "raw/",
                            "synchronized_frames": sync_count,
                            "event_evaluator": "openaladdin-python-event-engine-v1",
                        }
                        derive_events_from_state(
                            run_dir / "state.jsonl",
                            run_dir / "events.jsonl",
                            end_frame=len(input_records),
                        )
                        capture_event_checkpoints(
                            run_dir, rom, manifest, run_dir / "events.jsonl"
                        )
                        final_status, valid = _finish_record_manifest(
                            run_dir, manifest, 0
                        )
                    except SystemExit as error:
                        print(str(error), file=sys.stderr)
                        final_status, valid = _finish_record_manifest(
                            run_dir, manifest, 1
                        )
    if valid:
        print(f"record: {run_dir}")
        print(f"record: frames {manifest['frames']}")
    return final_status

def command_mame(args: argparse.Namespace) -> int:
    """Launch a general interactive MAME session through the project wrapper."""
    rom = resolve(args.rom)
    if not rom.is_file():
        raise SystemExit(f"ROM not found: {rom}")
    if args.frames is not None and args.frames < 1:
        raise SystemExit("--frames must be positive when supplied")
    if args.mame_record and args.mame_playback:
        raise SystemExit("--mame-record and --mame-playback are mutually exclusive")
    if args.input and args.mame_playback:
        raise SystemExit("--input cannot be combined with --mame-playback")

    trace_dir = resolve(args.trace_dir) if args.trace_dir else ROOT / "build/re/mame-session"
    environment = _clean_mame_environment()
    environment.update({
        "OPENALADDIN_TRACE_DIR": str(trace_dir),
        # The interactive launcher should not stop after the wrapper's normal
        # 120-frame trace default. A supplied count means that many captured
        # logical frames, including frame zero.
        "OPENALADDIN_TRACE_FRAMES": (
            "-1" if args.frames is None else str(args.frames - 1)
        ),
        "OPENALADDIN_CAPTURE": args.capture,
        "OPENALADDIN_STATE_OUTPUT": "1",
        "OPENALADDIN_EVENT_OUTPUT": str(trace_dir / "events.jsonl"),
        "OPENALADDIN_EVENT_SPEC": event_detector_protocol() or "",
        "OPENALADDIN_MAME_HEADLESS": "1" if args.headless else "0",
        "OPENALADDIN_MAME_VIDEO": args.video,
        "OPENALADDIN_EXECUTION_PROFILE": "analysis" if args.headless else "interactive",
        "OPENALADDIN_MAME_SOUND": "none" if args.headless else "sdl",
        "OPENALADDIN_MAME_DEBUG_UI": "1" if args.debug_ui else "0",
        "OPENALADDIN_INPUT_MODE": "inject" if args.input else "record",
        "OPENALADDIN_ROM_SHA256": hashes(rom)["sha256"],
    })
    if args.input:
        environment["OPENALADDIN_INPUT"] = args.input
    if args.load_state:
        environment["OPENALADDIN_LOAD_STATE"] = str(resolve(Path(args.load_state)))
    if args.checkpoints:
        environment["OPENALADDIN_CHECKPOINTS"] = args.checkpoints
    if args.mame_record:
        environment["OPENALADDIN_RECORD_FILE"] = str(resolve(Path(args.mame_record)))
    if args.mame_playback:
        environment["OPENALADDIN_PLAYBACK_FILE"] = str(resolve(Path(args.mame_playback)))

    print(f"mame: launching {rom}")
    print(f"mame: trace output {trace_dir}")
    return run_shell_tool("mame/run.sh", [str(rom)], env=environment)

__all__ = [name for name in globals() if not name.startswith("__")]
