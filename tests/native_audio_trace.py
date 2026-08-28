#!/usr/bin/env python3
"""Smoke-test native audio trace emission through the real executable."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/openaladdin"


def run_trace(trace: Path, frames: int, sound_id: str | None = None) -> list[dict]:
    command = [
        str(BINARY),
        "--no-window",
        "--frames",
        str(frames),
        "--audio-trace",
        str(trace),
    ]
    if sound_id is not None:
        command.extend(["--sound-id", sound_id])
    environment = dict(os.environ)
    environment["SDL_VIDEODRIVER"] = "dummy"
    environment["SDL_AUDIODRIVER"] = "dummy"
    result = subprocess.run(
        command,
        cwd=ROOT,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    return [json.loads(line) for line in trace.read_text(encoding="utf-8").splitlines()]


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="openaladdin-native-audio-") as directory:
        trace = Path(directory) / "audio.jsonl"
        records = run_trace(trace, 8)

        sfx_trace = Path(directory) / "sfx.jsonl"
        sfx_records = run_trace(sfx_trace, 40, "0x4c")
        sfx_psg_writes = [
            record["byte"]
            for record in sfx_records
            if record["type"] == "audio_write" and record["kind"] == "psg"
        ]
        assert sfx_psg_writes == [
            0x9F, 0xBF, 0xDF, 0xFF,
            0xC7, 0x08, 0xE7, 0xDF, 0xFE,
            0xFC, 0xFA, 0xF8, 0xF6, 0xF4, 0xF2,
            0xF0, 0xF0, 0xF0,
            0xDF, 0xFF,
        ]

    assert records[0]["type"] == "header"
    assert records[0]["format"] == "openaladdin-native-audio-trace-v1"
    commands = [record for record in records if record["type"] == "audio_command"]
    events = [record for record in records if record["type"] == "driver_event"]
    writes = [record for record in records if record["type"] == "audio_write"]
    assert any(record["opcode"] == 0x0B for record in commands)
    assert any(record["opcode"] == 0x10 and record["sound_id"] == 0x49 for record in commands)
    assert events
    assert writes
    assert all(record["source"] == "z80" for record in writes)
    assert [record["sequence"] for record in records[1:]] == list(range(len(records) - 1))

    print("native audio trace: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
