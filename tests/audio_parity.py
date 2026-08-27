#!/usr/bin/env python3
"""Exercise the normalized MAME/native audio parity comparator."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from genie.mame.audio_parity import (  # noqa: E402
    compare_records,
    mame_write_records,
    native_records,
    normalized_mame_writes,
    normalized_native_commands,
    normalized_native_writes,
    normalized_mame_commands,
)


def write_jsonl(path: Path, records: list[dict]) -> None:
    path.write_text(
        "".join(json.dumps(record) + "\n" for record in records),
        encoding="utf-8",
    )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="openaladdin-audio-parity-") as temp:
        root = Path(temp)
        mame = root / "mame"
        mame.mkdir()
        write_jsonl(mame / "sound_writes.jsonl", [
            {
                "type": "audio_write",
                "frame": 7,
                "kind": "ym2612",
                "source": "z80",
                "port": 0,
                "data": 0xA0,
                "byte": 0xA0,
            },
            {
                "type": "audio_write",
                "frame": 7,
                "kind": "ym2612",
                "source": "z80",
                "port": 1,
                "data": 0x24,
                "byte": 0x24,
            },
            {
                "type": "audio_write",
                "frame": 8,
                "kind": "psg",
                "source": "z80",
                "port": 17,
                "data": 0x90,
                "byte": 0x90,
            },
            {
                "type": "audio_write",
                "frame": 8,
                "kind": "psg",
                "source": "maincpu",
                "port": 16,
                "data": 0x80,
                "byte": 0x80,
            },
        ])
        (mame / "debug.log").write_text(
            "OPENALADDIN_AUDIO_COMMAND KIND=SFX ID=0000004C "
            "PC=001AC9D8 FRAME=0000000A\n",
            encoding="utf-8",
        )

        native = root / "native.jsonl"
        write_jsonl(native, [
            {"type": "header", "format": "openaladdin-native-audio-trace-v1"},
            {
                "type": "audio_command",
                "frame": 5,
                "kind": "SFX",
                "sound_id": 0x4C,
            },
            {
                "type": "driver_event",
                "frame": 7,
                "kind": "note",
                "channel": 0,
            },
            {
                "type": "audio_write",
                "frame": 7,
                "kind": "ym2612",
                "source": "z80",
                "port": 0,
                "data": 0xA0,
                "byte": 0xA0,
            },
            {
                "type": "audio_write",
                "frame": 7,
                "kind": "ym2612",
                "source": "z80",
                "port": 1,
                "data": 0x24,
                "byte": 0x24,
            },
            {
                "type": "audio_write",
                "frame": 8,
                "kind": "psg",
                "source": "z80",
                "data": 0x90,
                "byte": 0x90,
            },
        ])

        mame_records = mame_write_records(mame, "z80")
        native_data = native_records(native)
        assert normalized_mame_writes(mame_records) == normalized_native_writes(native_data)
        assert normalized_mame_commands(mame) == [(10, "SFX", 0x4C)]
        assert normalized_native_commands(native_data, frame_offset=5) == [(10, "SFX", 0x4C)]
        assert compare_records(
            "fixture writes",
            normalized_mame_writes(mame_records),
            normalized_native_writes(native_data),
        )

        changed = list(normalized_native_writes(native_data))
        changed[-1] = (8, "psg", None, 0x91)
        assert not compare_records(
            "fixture mismatch",
            normalized_mame_writes(mame_records),
            changed,
        )

    print("audio parity: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
