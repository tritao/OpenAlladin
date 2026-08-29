from __future__ import annotations

from genie.assets.scene_transitions import decode_scene_resource_streams


def test_scene_resource_streams_are_fixed_adjacent_and_terminated():
    data = bytearray(0x50)
    table = {
        "address": "0x00",
        "entry_size": 6,
        "count": 2,
        "pointer_offset": 0,
        "state_offset": 4,
        "metadata_offset": 5,
    }
    data[0:4] = (0x0C).to_bytes(4, "big")
    data[4] = 0
    data[6:10] = (0x1C).to_bytes(4, "big")
    data[10] = 2
    data[0x1A:0x1C] = b"\x00\x00"
    data[0x2A:0x2C] = b"\x00\x00"

    streams = decode_scene_resource_streams(
        bytes(data),
        table,
        {"start": "0x0C", "end_exclusive": "0x2C", "stream_size": "0x10"},
    )

    assert [item["entry"] for item in streams] == ["0x00000C", "0x00001C"]
    assert [item["state"] for item in streams] == ["0x00", "0x02"]
    assert all(item["terminal_bytes"] == "0000" for item in streams)
