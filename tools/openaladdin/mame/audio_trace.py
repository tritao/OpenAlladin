#!/usr/bin/env python3
"""Summarize a MAME audio-bus trace and ROM sound-command log."""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re
from typing import Any


COMMAND_RE = re.compile(
    r"OPENALADDIN_AUDIO_COMMAND KIND=(?P<kind>\S+) "
    r"ID=(?P<id>[0-9A-Fa-f]+) PC=(?P<pc>[0-9A-Fa-f]+) "
    r"FRAME=(?P<frame>[0-9A-Fa-f]+)(?P<rest>.*)$"
)
MAILBOX_RE = re.compile(
    r"OPENALADDIN_AUDIO_MAILBOX ADDR=(?P<address>[0-9A-Fa-f]+) "
    r"DATA=(?P<data>[0-9A-Fa-f]+) PC=(?P<pc>[0-9A-Fa-f]+) "
    r"FRAME=(?P<frame>[0-9A-Fa-f]+) D0=(?P<d0>[0-9A-Fa-f]+) "
    r"A0=(?P<a0>[0-9A-Fa-f]+)"
)
MAILBOX_DATA_RE = re.compile(
    r"OPENALADDIN_AUDIO_MAILBOX_DATA ADDR=(?P<address>[0-9A-Fa-f]+) "
    r"DATA=(?P<data>[0-9A-Fa-f]+) PC=(?P<pc>[0-9A-Fa-f]+) "
    r"FRAME=(?P<frame>[0-9A-Fa-f]+) D0=(?P<d0>[0-9A-Fa-f]+) "
    r"D1=(?P<d1>[0-9A-Fa-f]+) A0=(?P<a0>[0-9A-Fa-f]+) "
    r"A1=(?P<a1>[0-9A-Fa-f]+)"
)
MAILBOX_READ_RE = re.compile(
    r"OPENALADDIN_AUDIO_MAILBOX_READ ADDR=(?P<address>[0-9A-Fa-f]+) "
    r"DATA=(?P<data>[0-9A-Fa-f]+) VISIBLE_PC=(?P<visible_pc>[0-9A-Fa-f]+) "
    r"FRAME=(?P<frame>[0-9A-Fa-f]+)"
)
DISPATCH_RE = re.compile(
    r"OPENALADDIN_AUDIO_DISPATCH KIND=(?P<kind>\S+) "
    r"PC=(?P<pc>[0-9A-Fa-f]+) FRAME=(?P<frame>[0-9A-Fa-f]+) "
    r"D0=(?P<d0>[0-9A-Fa-f]+) D1=(?P<d1>[0-9A-Fa-f]+) "
    r"A0=(?P<a0>[0-9A-Fa-f]+) A1=(?P<a1>[0-9A-Fa-f]+) "
    r"A2=(?P<a2>[0-9A-Fa-f]+)"
)


QUEUE_BASE = 0xA01B40
QUEUE_SIZE = 0x40
QUEUE_PACKET_OPCODES = {
    0x10: "send",
    0x12: "prepare",
    0x16: "control_no_id",
}


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    records = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"{path}:{line_number}: invalid JSON: {error}") from error
        if not isinstance(record, dict):
            raise ValueError(f"{path}:{line_number}: expected an object")
        records.append(record)
    return records


def read_commands(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    commands = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = COMMAND_RE.search(line)
        if not match:
            continue
        commands.append({
            "kind": match.group("kind"),
            "id": int(match.group("id"), 16),
            "pc": int(match.group("pc"), 16),
            "frame": int(match.group("frame"), 16),
            "raw": line,
            "line": line_number,
        })
    return commands


def read_mailbox(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    records = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = MAILBOX_RE.search(line)
        if not match:
            continue
        records.append({
            "type": "sound_mailbox_write",
            "kind": "z80_ram",
            "source": "maincpu",
            "address": int(match.group("address"), 16),
            "offset": int(match.group("address"), 16) - 0xA00000,
            "data": int(match.group("data"), 16),
            "byte": int(match.group("data"), 16) & 0xff,
            "pc": int(match.group("pc"), 16),
            "frame": int(match.group("frame"), 16),
            "d0": int(match.group("d0"), 16),
            "a0": int(match.group("a0"), 16),
            "line": line_number,
            "raw": line,
        })
    return records


def read_mailbox_data(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    records = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = MAILBOX_DATA_RE.search(line)
        if not match:
            continue
        records.append({
            "type": "sound_mailbox_data",
            "kind": "z80_ram",
            "source": "maincpu",
            "address": int(match.group("address"), 16),
            "offset": int(match.group("address"), 16) - 0xA00000,
            "data": int(match.group("data"), 16),
            "byte": int(match.group("data"), 16) & 0xff,
            "pc": int(match.group("pc"), 16),
            "frame": int(match.group("frame"), 16),
            "d0": int(match.group("d0"), 16),
            "d1": int(match.group("d1"), 16),
            "a0": int(match.group("a0"), 16),
            "a1": int(match.group("a1"), 16),
            "line": line_number,
            "raw": line,
        })
    return records


def next_queue_address(address: int) -> int:
    offset = (address - QUEUE_BASE + 1) & (QUEUE_SIZE - 1)
    return QUEUE_BASE + offset


def decode_mailbox_packets(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Decode the observed FF/opcode/id records emitted by the 68K queue helpers."""
    packets = []
    index = 0
    while index + 1 < len(records):
        marker = records[index]
        opcode = records[index + 1]
        adjacent = (
            marker.get("byte") == 0xFF
            and opcode.get("address") == next_queue_address(marker["address"])
        )
        if adjacent and opcode["byte"] == 0x16:
            packets.append({
                "type": "sound_mailbox_packet",
                "frame": marker["frame"],
                "address": marker["address"],
                "offset": marker["address"] - QUEUE_BASE,
                "marker": marker["byte"],
                "opcode": opcode["byte"],
                "opcode_name": QUEUE_PACKET_OPCODES[opcode["byte"]],
                "command_id": None,
                "bytes": [marker["byte"], opcode["byte"]],
                "pc": marker["pc"],
                "write_lines": [marker["line"], opcode["line"]],
            })
            index += 2
        elif adjacent and index + 2 < len(records):
            command = records[index + 2]
            if command.get("address") != next_queue_address(opcode["address"]):
                index += 1
                continue
            packets.append({
                "type": "sound_mailbox_packet",
                "frame": marker["frame"],
                "address": marker["address"],
                "offset": marker["address"] - QUEUE_BASE,
                "marker": marker["byte"],
                "opcode": opcode["byte"],
                "opcode_name": QUEUE_PACKET_OPCODES.get(opcode["byte"], "unknown"),
                "command_id": command["byte"],
                "bytes": [marker["byte"], opcode["byte"], command["byte"]],
                "pc": marker["pc"],
                "write_lines": [marker["line"], opcode["line"], command["line"]],
            })
            index += 3
        else:
            index += 1
    return packets


def read_dispatches(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    dispatches = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = DISPATCH_RE.search(line)
        if not match:
            continue
        dispatches.append({
            "kind": match.group("kind"),
            "pc": int(match.group("pc"), 16),
            "frame": int(match.group("frame"), 16),
            "d0": int(match.group("d0"), 16),
            "d1": int(match.group("d1"), 16),
            "a0": int(match.group("a0"), 16),
            "a1": int(match.group("a1"), 16),
            "a2": int(match.group("a2"), 16),
            "raw": line,
            "line": line_number,
        })
    return dispatches


def read_mailbox_reads(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    reads = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = MAILBOX_READ_RE.search(line)
        if not match:
            continue
        reads.append({
            "type": "sound_mailbox_read",
            "kind": "z80_ram",
            "source": "z80",
            "address": int(match.group("address"), 16),
            "data": int(match.group("data"), 16),
            "visible_pc": int(match.group("visible_pc"), 16),
            "frame": int(match.group("frame"), 16),
            "raw": line,
            "line": line_number,
        })
    return reads


def summarize(trace_dir: Path) -> dict[str, Any]:
    writes = read_jsonl(trace_dir / "sound_writes.jsonl")
    mailbox = read_jsonl(trace_dir / "sound_mailbox.jsonl")
    if not mailbox or any("kind" not in record or "source" not in record for record in mailbox):
        mailbox = read_mailbox(trace_dir / "debug.log")
    mailbox_data = read_mailbox_data(trace_dir / "debug.log")
    mailbox_packets = decode_mailbox_packets(mailbox_data)
    commands = read_commands(trace_dir / "debug.log")
    dispatches = read_dispatches(trace_dir / "debug.log")
    mailbox_reads = read_mailbox_reads(trace_dir / "debug.log")
    write_kinds = Counter(str(record.get("kind", "unknown")) for record in writes)
    write_sources = Counter(str(record.get("source", "unknown")) for record in writes)
    command_kinds = Counter(str(record.get("kind", "unknown")) for record in commands)
    command_ids = Counter(
        f"{record['id']:02X}"
        for record in commands
    )
    dispatch_kinds = Counter(str(record.get("kind", "unknown")) for record in dispatches)
    dispatch_ids = Counter(
        f"{record['d0']:02X}"
        for record in dispatches
        if record.get("kind") == "SEND"
    )
    ym_ports = Counter(
        f"{int(record.get('port', 0)):02X}"
        for record in writes
        if record.get("kind") == "ym2612"
    )
    mailbox_kinds = Counter(str(record.get("kind", "unknown")) for record in mailbox)
    mailbox_sources = Counter(str(record.get("source", "unknown")) for record in mailbox)
    mailbox_data_addresses = Counter(f"{int(record['address']):08X}" for record in mailbox_data)
    mailbox_packet_opcodes = Counter(f"{int(record['opcode']):02X}" for record in mailbox_packets)
    mailbox_packet_ids = Counter(
        f"{int(record['command_id']):02X}"
        for record in mailbox_packets
        if record.get("command_id") is not None
    )
    mailbox_read_addresses = Counter(f"{int(record['address']):04X}" for record in mailbox_reads)

    return {
        "format": "openaladdin-audio-trace-v1",
        "trace_dir": str(trace_dir),
        "sound_writes": writes,
        "sound_mailbox": mailbox,
        "sound_mailbox_data": mailbox_data,
        "sound_mailbox_packets": mailbox_packets,
        "sound_mailbox_reads": mailbox_reads,
        "commands": commands,
        "dispatches": dispatches,
        "summary": {
            "sound_write_count": len(writes),
            "sound_write_kinds": dict(sorted(write_kinds.items())),
            "sound_write_sources": dict(sorted(write_sources.items())),
            "ym2612_port_counts": dict(sorted(ym_ports.items())),
            "sound_mailbox_count": len(mailbox),
            "sound_mailbox_kinds": dict(sorted(mailbox_kinds.items())),
            "sound_mailbox_sources": dict(sorted(mailbox_sources.items())),
            "sound_mailbox_data_count": len(mailbox_data),
            "sound_mailbox_data_addresses": dict(sorted(mailbox_data_addresses.items())),
            "sound_mailbox_packet_count": len(mailbox_packets),
            "sound_mailbox_packet_opcodes": dict(sorted(mailbox_packet_opcodes.items())),
            "sound_mailbox_packet_ids": dict(sorted(mailbox_packet_ids.items())),
            "sound_mailbox_read_count": len(mailbox_reads),
            "sound_mailbox_read_addresses": dict(sorted(mailbox_read_addresses.items())),
            "command_count": len(commands),
            "command_kinds": dict(sorted(command_kinds.items())),
            "command_ids": dict(sorted(command_ids.items())),
            "dispatch_count": len(dispatches),
            "dispatch_kinds": dict(sorted(dispatch_kinds.items())),
            "dispatch_ids": dict(sorted(dispatch_ids.items())),
            "first_sound_write_frame": min((int(record.get("frame", 0)) for record in writes), default=None),
            "last_sound_write_frame": max((int(record.get("frame", 0)) for record in writes), default=None),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace_dir", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = summarize(args.trace_dir.resolve())
    output = args.output.resolve() if args.output else args.trace_dir / "audio_summary.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if report["sound_mailbox"]:
        mailbox_path = output.parent / "sound_mailbox.jsonl"
        mailbox_path.write_text(
            "".join(json.dumps(record, separators=(",", ":")) + "\n" for record in report["sound_mailbox"]),
            encoding="utf-8",
        )
    print(f"audio writes: {report['summary']['sound_write_count']}")
    print(f"audio commands: {report['summary']['command_count']}")
    print(f"audio dispatches: {report['summary']['dispatch_count']}")
    print(f"audio mailbox writes: {report['summary']['sound_mailbox_count']}")
    print(f"audio mailbox data writes: {report['summary']['sound_mailbox_data_count']}")
    print(f"audio mailbox packets: {report['summary']['sound_mailbox_packet_count']}")
    print(f"summary: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
