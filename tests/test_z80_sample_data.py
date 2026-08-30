from __future__ import annotations

from genie.common import ROOT, load_yaml, parse_int
from genie.games.aladdin.mame.z80_sound import (
    SEQUENCE_TABLE_BASE,
    SAMPLE_DESCRIPTOR_TABLE_BASE,
    SOUND_TEST_ENTRY_COUNT,
    SOUND_TEST_ENTRY_TABLE_BASE,
    sequence_table_report,
    sound_test_table_report,
    sequence_stream_ranges,
)


def test_z80_sample_descriptors_bound_contiguous_waveform_payload():
    rom = (ROOT / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    knowledge = load_yaml(ROOT / "re/sound/z80-driver.yml")
    table = knowledge["sample_descriptor_table"]
    base = parse_int(table["address"])
    assert base == SAMPLE_DESCRIPTOR_TABLE_BASE
    assert SEQUENCE_TABLE_BASE == 0x001BAF6F
    assert parse_int(table["entry_width"]) == 12
    assert parse_int(table["entry_count"]) == 30

    previous_end = None
    for index in range(parse_int(table["entry_count"])):
        address = base + index * parse_int(table["entry_width"])
        record = rom[address:address + 12]
        offset = int.from_bytes(record[1:4], "little")
        length = int.from_bytes(record[6:8], "little")
        if previous_end is not None:
            assert offset == previous_end
        previous_end = offset + length

    payload_start = parse_int(table["payload_start"])
    payload_end = parse_int(table["payload_end_inclusive"])
    assert base + 0x168 == payload_start
    assert base + previous_end - 1 == payload_end
    assert payload_end == 0x001E56BE
    assert len(rom[payload_start:payload_end + 1]) == parse_int(table["payload_size"])


def test_z80_sequence_streams_are_non_overlapping_and_terminal():
    rom = (ROOT / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    streams = sequence_stream_ranges(rom)
    assert len(streams) == 297
    previous_end = None
    for stream in streams:
        start = parse_int(stream["start"])
        end = parse_int(stream["end_inclusive"])
        assert stream["terminator"] == "0x60"
        assert end >= start
        if previous_end is not None:
            assert start == previous_end + 1
        previous_end = end
    assert streams[0]["start"] == "0x1BB317"
    assert streams[-1]["end_inclusive"] == "0x1C73CA"
    assert sum(parse_int(stream["size"]) for stream in streams) == 0xC0B4


def test_sound_test_labels_name_sequence_ids():
    rom = (ROOT / "rom/Disneys_Aladdin_U_p1.bin").read_bytes()
    sound_test = sound_test_table_report(rom)
    assert parse_int(sound_test["table_address"]) == SOUND_TEST_ENTRY_TABLE_BASE
    assert sound_test["entry_width"] == 0x10
    assert sound_test["entry_count"] == SOUND_TEST_ENTRY_COUNT == 94
    assert sound_test["sentinel_address"] == "0x126D3E"

    entries = sound_test["entries"]
    assert len(entries) == SOUND_TEST_ENTRY_COUNT
    assert entries["0x31"]["name"] == "ALADDIN HURT"
    assert entries["0x49"]["name"] == "PRINCE ALI"
    assert entries["0x4C"]["name"] == "FIRE FROM COAL"

    sequences = sequence_table_report(rom)["entries"]
    assert sequences["0x31"]["name"] == "ALADDIN HURT"
    assert sequences["0x49"]["name"] == "PRINCE ALI"
    assert sequences["0x4C"]["name"] == "FIRE FROM COAL"
    assert sequences["0x15"]["name"] is None
    assert sum(entry["name"] is None for entry in sequences.values()) == 20
