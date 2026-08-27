"""MAME run capture, replay, and manifest services."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
from typing import Any

from genie.runtime import *
from genie.mame.experiments import event_detector_protocol, derive_events_from_state, capture_event_checkpoints
from genie.mame.state import *
MAME_RUN_ENVIRONMENT = (
    "OPENALADDIN_TRACE_DIR",
    "OPENALADDIN_TRACE_FRAMES",
    "OPENALADDIN_INPUT",
    "OPENALADDIN_INPUT_MODE",
    "OPENALADDIN_INPUT_OUTPUT",
    "OPENALADDIN_EVENT_OUTPUT",
    "OPENALADDIN_EVENT_SPEC",
    "OPENALADDIN_STATE_OUTPUT",
    "OPENALADDIN_CAPTURE",
    "OPENALADDIN_CAPTURE_VDP",
    "OPENALADDIN_TRACE_ACTORS",
    "OPENALADDIN_TRACE_ACTOR_INIT",
    "OPENALADDIN_TRACE_RNC_LOADS",
    "OPENALADDIN_STATE_SYNC",
    "OPENALADDIN_TRACE_EDGES",
    "OPENALADDIN_TRACE_AUDIO",
    "OPENALADDIN_TRACE_AUDIO_MAILBOX",
    "OPENALADDIN_TRACE_AUDIO_MAILBOX_READS",
    "OPENALADDIN_AUDIO_MAILBOX_READ_FRAMES",
    "OPENALADDIN_TRACE_AUDIO_COMMANDS",
    "OPENALADDIN_DEBUG_WATCH",
    "OPENALADDIN_BREAKPOINTS",
    "OPENALADDIN_LOAD_STATE",
    "OPENALADDIN_PRELOAD_STATE",
    "OPENALADDIN_PRELOAD_BEFORE_CAPTURE",
    "OPENALADDIN_CHECKPOINTS",
    "OPENALADDIN_CHECKPOINT_REFERENCE",
    "OPENALADDIN_STATE_DIRECTORY",
    "OPENALADDIN_INPUT_DIRECTORY",
    "OPENALADDIN_RECORD_FILE",
    "OPENALADDIN_PLAYBACK_FILE",
    "OPENALADDIN_MAME_HEADLESS",
    "OPENALADDIN_MAME_VIDEO",
    "OPENALADDIN_MAME_DEBUG_UI",
    "OPENALADDIN_MAME_SOUND",
    "OPENALADDIN_EXECUTION_PROFILE",
)

def _clean_mame_environment() -> dict[str, str]:
    environment = os.environ.copy()
    for key in MAME_RUN_ENVIRONMENT:
        environment.pop(key, None)
    return environment

def _copy_artifact(source: Path, destination: Path) -> bool:
    """Copy one completed capture artifact, reporting whether it existed."""
    if not source.is_file():
        return False
    if source.resolve() == destination.resolve():
        return True
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    return True

def _materialize_record_capture(
    run_dir: Path,
    capture_dir: Path,
    *,
    rom_path: Path | None = None,
) -> int:
    """Turn a raw post-recording capture into compatibility artifacts.

    MAME writes only below ``capture_dir``.  The raw files remain untouched;
    the root-level files are the established semantic compatibility view used
    by the existing replay and analysis commands.
    """
    raw_state = capture_dir / "state.jsonl"
    if not raw_state.is_file():
        raise SystemExit(f"record: missing raw synchronized capture {raw_state}")
    _copy_artifact(raw_state, run_dir / "state.raw.jsonl")
    sync_count = synchronize_state_trace(capture_dir, run_dir, rom_path=rom_path)

    raw_events = capture_dir / "events.jsonl"
    if raw_events.is_file():
        _copy_artifact(raw_events, run_dir / "events.raw.jsonl")

    raw_trace = capture_dir / "trace_boot.jsonl"
    if raw_trace.is_file():
        _copy_artifact(raw_trace, run_dir / "trace_boot.raw.jsonl")
        _copy_artifact(raw_trace, run_dir / "trace_boot.jsonl")
    raw_input = capture_dir / "input.jsonl"
    if raw_input.is_file():
        _copy_artifact(raw_input, run_dir / "capture-input.jsonl")
    return sync_count

def _write_trace_quality(
    run_dir: Path,
    frames: int,
    *,
    capture_status: str,
) -> dict[str, Any]:
    """Summarize continuity, synchronization, and provenance for a run."""
    state_path = run_dir / "state.jsonl"
    state_frames = 0
    missing_state: list[int] = []
    sync: dict[str, Any] = {
        "boundary": "unavailable",
        "coverage": 0.0,
    }
    normalization: list[dict[str, Any]] = []
    if state_path.is_file():
        header, states, _ = load_state_trace(state_path)
        state_frames = len(states)
        missing_state = [frame for frame in range(frames) if frame not in states]
        sync = dict(header.get("sync") or sync)
        normalization = list(header.get("transformations") or [])

    input_replay = "unavailable"
    captured_input = run_dir / "capture-input.jsonl"
    if captured_input.is_file() and (run_dir / "input.jsonl").is_file():
        try:
            _, source_records = load_input_timeline(run_dir / "input.jsonl")
            _, replay_records = load_input_timeline(captured_input)
            input_replay = (
                "pass"
                if [record["mask"] for record in source_records]
                == [record["mask"] for record in replay_records[:frames]]
                else "fail"
            )
        except SystemExit:
            input_replay = "fail"

    events_count = 0
    checkpoint_count = 0
    checkpoint_hashes = 0
    events_path = run_dir / "events.jsonl"
    if events_path.is_file():
        _, events = load_event_timeline(events_path)
        events_count = len(events)
        for event in events:
            state_reference = str(event.get("state", ""))
            checkpoint = _event_state_path(run_dir, state_reference) if state_reference else None
            if checkpoint is not None:
                checkpoint_count += 1
                if checkpoint.is_file():
                    event["state_sha256"] = hashes(checkpoint)["sha256"]
                    checkpoint_hashes += 1

    quality = "recorded"
    if capture_status == "pass":
        quality = "captured"
        if not missing_state and sync.get("coverage", 0) >= 1.0:
            quality = "deterministic"
        if quality == "deterministic" and input_replay == "pass" and checkpoint_hashes == checkpoint_count:
            quality = "semantic-verified"
    report = {
        "format": "openaladdin-trace-quality-v2",
        "input_frames": frames,
        "state_frames": state_frames,
        "missing_state_frames": missing_state,
        "sync": sync,
        "normalization": normalization,
        "events": {
            "count": events_count,
            "invalid": 0,
        },
        "checkpoints": {
            "count": checkpoint_count,
            "hash_verified": checkpoint_hashes,
        },
        "determinism": {
            "mame_inp_replay": capture_status,
            "json_input_replay": input_replay,
            "semantic_state_roundtrip": "unavailable",
        },
        "quality": quality,
    }
    _write_json(run_dir / "trace-quality.json", report)
    return report

def _update_trace_quality_roundtrip(run_dir: Path, status: str) -> None:
    """Record the result of replaying mame.inp into the quality report."""
    path = run_dir / "trace-quality.json"
    if not path.is_file():
        return
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return
    determinism = report.setdefault("determinism", {})
    determinism["semantic_state_roundtrip"] = status
    if status == "pass" and report.get("quality") == "semantic-verified":
        report["quality"] = "parity-ready"
    _write_json(path, report)
    manifest_path = run_dir / "run.json"
    if manifest_path.is_file():
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            return
        manifest["quality"] = report.get("quality", manifest.get("quality"))
        _write_json(manifest_path, manifest)

def _finish_record_manifest(
    run_dir: Path,
    manifest: dict[str, Any],
    status: int,
    capture_dir: Path | None = None,
) -> tuple[int, bool]:
    input_path = run_dir / "input.jsonl"
    valid = True
    frames = 0
    if input_path.is_file():
        try:
            _, records = load_input_timeline(input_path)
            frames = len(records)
        except SystemExit as error:
            print(f"record: invalid input timeline: {error}", file=sys.stderr)
            valid = False
    else:
        valid = False
        print(f"record: missing {input_path}", file=sys.stderr)
    state_path = run_dir / "state.jsonl"
    if status == 0 and capture_dir is not None:
        try:
            sync_count = _materialize_record_capture(
                run_dir, capture_dir, rom_path=resolve(Path(str(manifest["rom_path"])))
            )
            manifest["capture"] = {
                "status": "complete",
                "execution_profile": "analysis",
                "raw_directory": "raw/",
                "synchronized_frames": sync_count,
            }
        except SystemExit as error:
            print(str(error), file=sys.stderr)
            valid = False
    elif status == 0 and state_path.is_file():
        # Legacy/direct callers can still finalize an already materialized
        # trace, but retain its original bytes before deriving a normalized
        # view. New recordings always use _materialize_record_capture above.
        raw_state = run_dir / "state.raw.jsonl"
        if not raw_state.is_file():
            _copy_artifact(state_path, raw_state)
            semantic = run_dir / "state.semantic.jsonl"
            normalize_animation_state_trace(state_path, semantic)
            if semantic.is_file():
                _copy_artifact(semantic, state_path)
    elif status == 0:
        valid = False
        print(f"record: missing {state_path}", file=sys.stderr)
    event_count = 0
    segment_count = 0
    if valid:
        try:
            event_count, segment_count = _write_segments(run_dir, frames)
        except SystemExit as error:
            print(f"record: invalid event timeline: {error}", file=sys.stderr)
            valid = False
    manifest["events"] = event_count
    manifest["segments"] = segment_count
    manifest["frames"] = frames
    quality = _write_trace_quality(
        run_dir,
        frames,
        capture_status="pass" if status == 0 and valid else "fail",
    )
    manifest["quality"] = quality["quality"]
    manifest.setdefault("artifacts", {})["quality"] = "trace-quality.json"
    manifest["status"] = "complete" if status == 0 and valid and frames > 0 else "failed"
    manifest["ended_at"] = datetime.now(timezone.utc).isoformat()
    _write_json(run_dir / "run.json", manifest)
    return (0 if status == 0 and valid and frames > 0 else (status or 1), valid)

def _load_run_manifest(name: str) -> tuple[Path, dict[str, Any]]:
    run_dir = run_directory(name)
    manifest_path = run_dir / "run.json"
    if not manifest_path.is_file():
        raise SystemExit(f"run manifest not found: {manifest_path}")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise SystemExit(f"{manifest_path}: invalid JSON: {error}") from error
    if manifest.get("format") != RUN_FORMAT:
        raise SystemExit(f"{manifest_path}: unsupported run format {manifest.get('format')!r}")
    return run_dir, manifest

def _run_rom(args: argparse.Namespace, manifest: dict[str, Any]) -> Path:
    if args.rom is not None:
        rom = resolve(args.rom)
    else:
        rom_path = Path(str(manifest.get("rom_path", "")))
        rom = resolve(rom_path) if not rom_path.is_absolute() else rom_path
    if not rom.is_file():
        raise SystemExit(f"ROM not found: {rom}")
    expected = manifest.get("rom_sha256")
    actual = hashes(rom)["sha256"]
    if expected and actual != expected:
        raise SystemExit(f"ROM mismatch for run: expected {expected}, got {actual}")
    return rom

def _update_replay_manifest(
    run_dir: Path,
    manifest: dict[str, Any],
    client: str,
    trace: Path,
    status: int,
    segment_id: str | None = None,
) -> None:
    replays = manifest.setdefault("replays", {})
    replay_key = client if segment_id is None else f"{client}:{segment_id}"
    replay = {
        "status": "complete" if status == 0 else "failed",
        "trace": _relative_to_root(trace),
        "updated_at": datetime.now(timezone.utc).isoformat(),
    }
    if segment_id is not None:
        replay["segment"] = segment_id
    replays[replay_key] = replay
    _write_json(run_dir / "run.json", manifest)

def _apply_run_start(environment: dict[str, str], manifest: dict[str, Any]) -> None:
    start = manifest.get("start") or {}
    if start.get("type") != "save_state":
        return
    state_path = Path(str(start.get("path", "")))
    state = resolve(state_path) if not state_path.is_absolute() else state_path
    if not state.is_file():
        raise SystemExit(f"recorded start state not found: {state}")
    expected = start.get("sha256")
    if expected:
        actual = hashes(state)["sha256"]
        if actual != expected:
            raise SystemExit(
                f"recorded start state mismatch: expected {expected}, got {actual}"
            )
    environment["OPENALADDIN_LOAD_STATE"] = str(state)

def _prepare_segment_replay(
    run_dir: Path,
    segment: dict[str, Any],
    input_header: dict[str, Any] | None,
    records: list[dict[str, Any]],
    replay_dir: Path,
    *,
    client: str = "mame",
) -> tuple[Path, dict[str, Any], list[str], int]:
    if client == "native":
        native_start_frame = segment.get("native_start_frame")
        if native_start_frame is None:
            readiness = segment.get("native_ready") or {}
            reason = readiness.get("reason", "no stable native boundary was found")
            raise SystemExit(
                f"segment {segment['id']!r} has no native parity boundary: {reason}"
            )
        start_frame = int(native_start_frame)
    else:
        start_frame = int(segment["start_frame"])
    end_frame = int(segment["end_frame"])
    segment_id = str(segment["id"])
    if end_frame >= len(records):
        raise SystemExit(
            f"segment {segment_id!r} ends at frame {end_frame}, "
            f"but the input timeline has {len(records)} frame(s)"
        )
    reference = replay_dir / "genesis.jsonl"
    initial_state = _write_sliced_state(
        run_dir / "state.jsonl",
        reference,
        start_frame,
        end_frame,
        segment_id,
    )
    # Keep derived segment references on the same stable VM boundary as the
    # recorder, including runs recorded before the normalizer was introduced.
    normalize_derived_state_trace(reference)
    _write_sliced_input(
        replay_dir / "input.jsonl",
        input_header,
        records,
        start_frame,
        end_frame,
        segment_id,
    )
    # The endpoint state is captured after the final update. The native
    # client consumes start..end-1; MAME also receives the endpoint token for
    # its final boundary sample, which preserves the recorded state label.
    tokens = input_tokens(records[start_frame:end_frame + 1])
    return reference, initial_state, tokens, end_frame - start_frame

def _mame_segment_input_tokens(tokens: list[str]) -> list[str]:
    """Return tokens aligned with the first frame after a loaded MAME state.

    ``OPENALADDIN_PRELOAD_BEFORE_CAPTURE`` loads the checkpoint before the
    initial capture, but the first emulated frame uses the controls applied by
    that initial capture.  The initial sample is discarded when the replay
    trace is rebased, so the first canonical segment token must be installed
    there; inserting a neutral bootstrap shifts every input edge by one frame.
    """
    return list(tokens)

__all__ = [name for name in globals() if not name.startswith("__")]
