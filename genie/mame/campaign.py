#!/usr/bin/env python3
"""Verify provenance and checkpoint completeness for recorded MAME campaigns."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"{path}:{line_number}: invalid JSON: {error}") from error
        if not isinstance(record, dict):
            raise ValueError(f"{path}:{line_number}: expected JSON object")
        records.append(record)
    return records


def repo_path(value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else ROOT / path


def verify(manifest_path: Path) -> int:
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    errors: list[str] = []

    rom = repo_path(str(document["rom"]))
    actual_hash = sha256(rom) if rom.is_file() else None
    if actual_hash != document.get("rom_sha256"):
        errors.append(f"ROM hash mismatch: expected {document.get('rom_sha256')}, got {actual_hash}")

    segment_count = 0
    checkpoint_count = 0
    for segment in document.get("segments", []):
        segment_count += 1
        trace_dir = repo_path(str(segment["trace_dir"]))
        state_trace_path = trace_dir / "state.jsonl"
        boot_trace_path = trace_dir / "trace_boot.jsonl"
        if not state_trace_path.is_file():
            errors.append(f"{segment['id']}: missing {state_trace_path}")
            continue
        if not boot_trace_path.is_file():
            errors.append(f"{segment['id']}: missing {boot_trace_path}")
        state_records = load_jsonl(state_trace_path)
        state_checkpoints = {
            (int(record["frame"]), str(record["name"]))
            for record in state_records
            if record.get("type") == "checkpoint" and "frame" in record and "name" in record
        }
        for checkpoint in segment.get("checkpoints", []):
            checkpoint_count += 1
            frame = int(checkpoint["frame"])
            name = str(checkpoint["name"])
            if (frame, name) not in state_checkpoints:
                errors.append(f"{segment['id']}: missing JSONL checkpoint {frame}={name}")
            state_path = repo_path(str(segment["trace_dir"])) / "states/genesis" / f"{name}.sta"
            if not state_path.is_file():
                errors.append(f"{segment['id']}: missing state file {state_path}")

        if segment.get("load_state"):
            source = repo_path(str(segment.get("loaded_state_source", "")))
            if not source.is_file():
                errors.append(f"{segment['id']}: missing loaded state source {source}")

    if errors:
        for error in errors:
            print(f"FAIL {error}")
        return 1
    print(f"OK {document['campaign']}: {segment_count} segment(s), {checkpoint_count} checkpoint(s), ROM hash verified")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("verify",))
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    return verify(args.manifest)


if __name__ == "__main__":
    raise SystemExit(main())
