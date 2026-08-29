"""Rank unresolved ROM layout gaps using existing offline evidence."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Iterable

from genie.common import ROOT, parse_int
from genie.ghidra.database import AnalysisDatabase

from .model import Layout, LayoutRange


def _address(value: Any) -> int:
    return parse_int(value)


def _hex(value: int) -> str:
    return f"0x{value:08X}"


def _read_json(path: Path) -> Any | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError, TypeError):
        return None


def _stream_values(document: Any) -> Iterable[tuple[str, dict[str, Any]]]:
    if not isinstance(document, dict):
        return ()
    streams = document.get("streams", {})
    if isinstance(streams, dict):
        return (
            (str(name), dict(value))
            for name, value in streams.items()
            if isinstance(value, dict)
        )
    if isinstance(streams, list):
        return (
            (str(value.get("name", "")), dict(value))
            for value in streams
            if isinstance(value, dict)
        )
    return ()


def _stream_extent(stream: dict[str, Any], entry: int) -> tuple[int, int]:
    try:
        size = _address(stream.get("bytes_decoded"))
    except (TypeError, ValueError):
        size = 0
    if size <= 0:
        ends: list[int] = []
        records = stream.get("instructions", []) or stream.get("steps", [])
        if isinstance(records, list):
            for record in records:
                if not isinstance(record, dict):
                    continue
                try:
                    address = _address(record["address"])
                    record_size = max(1, _address(record.get("size", 1)))
                except (KeyError, TypeError, ValueError):
                    continue
                ends.append(address + record_size - 1)
        return entry, max(ends, default=entry)
    return entry, entry + size - 1


def _load_streams(root: Path, kind: str, explicit: Path | None) -> list[dict[str, Any]]:
    paths = [explicit] if explicit is not None else [root / "build/re" / f"{kind}_streams.json"]
    if explicit is None and kind == "animation":
        paths.append(root / "build/assets/animations.json")
    result: list[dict[str, Any]] = []
    seen: set[int] = set()
    for path in paths:
        document = _read_json(path)
        for name, stream in _stream_values(document):
            try:
                entry = _address(stream["entry"])
            except (KeyError, TypeError, ValueError):
                continue
            if entry in seen:
                continue
            seen.add(entry)
            start, end = _stream_extent(stream, entry)
            result.append({
                "kind": kind,
                "name": name or None,
                "entry": entry,
                "start": start,
                "end": end,
                "bytes_decoded": max(0, end - start + 1),
                "stopped_reason": stream.get("stopped_reason"),
                "source": str(path),
            })
    return result


def _unknown_at(gaps: list[LayoutRange], address: int) -> LayoutRange | None:
    return next((gap for gap in gaps if gap.start <= address <= gap.end), None)


def _database_references(database: AnalysisDatabase) -> list[dict[str, Any]]:
    try:
        document = database.load("xrefs.json")
    except (FileNotFoundError, OSError, ValueError, TypeError):
        return []
    values = document.get("references", []) if isinstance(document, dict) else document
    return [dict(value) for value in values if isinstance(value, dict)]


def _template_pointer_evidence(
    layout: Layout,
    rom: bytes | None,
    gaps: list[LayoutRange],
) -> dict[int, list[dict[str, Any]]]:
    """Find unresolved pointers in the established 20-byte actor records."""

    if rom is None:
        return {}
    result: dict[int, list[dict[str, Any]]] = {}
    for item in layout.ranges:
        if item.layout_class != "ACTOR_TEMPLATE" or item.size < 16:
            continue
        if item.start + 16 > len(rom):
            continue
        for field, offset in (("movement_stream", 0x06), ("animation_stream", 0x0C)):
            target = int.from_bytes(rom[item.start + offset:item.start + offset + 4], "big")
            if target == 0 or target >= layout.rom_size:
                continue
            gap = _unknown_at(gaps, target)
            if gap is None:
                continue
            result.setdefault(gap.start, []).append({
                "kind": "actor_template_pointer",
                "template": _hex(item.start),
                "template_name": item.name,
                "field": field,
                "field_address": _hex(item.start + offset),
                "target": _hex(target),
                "gap": f"{_hex(gap.start)}-{_hex(gap.end)}",
            })
    return result


def build_layout_candidates(
    database: AnalysisDatabase | Path,
    layout: Layout,
    *,
    root: Path = ROOT,
    rom: bytes | None = None,
    animation_path: Path | None = None,
    movement_path: Path | None = None,
    max_references: int = 12,
) -> list[dict[str, Any]]:
    """Return ranked evidence records for unknown layout gaps.

    This is deliberately advisory.  It never changes ``layout.json`` or
    claims ownership of bytes.  Every proposed extent is tied to an existing
    pointer, reference, or decoder artifact so investigators can promote a
    batch into canonical symbols after review.
    """

    if not isinstance(database, AnalysisDatabase):
        database = AnalysisDatabase(Path(database))
    root = Path(root).resolve()
    gaps = list(layout.gaps())
    by_start: dict[int, dict[str, Any]] = {
        gap.start: {
            "gap": {
                "start": _hex(gap.start),
                "end": _hex(gap.end),
                "size": gap.size,
            },
            "evidence": [],
            "proposed_ranges": [],
            "reasons": [],
        }
        for gap in gaps
    }

    for reference in _database_references(database):
        try:
            target = _address(reference.get("to"))
        except (TypeError, ValueError):
            continue
        gap = _unknown_at(gaps, target)
        if gap is None:
            continue
        row = {
            "kind": "direct_reference",
            "from": _hex(_address(reference.get("from", 0))),
            "to": _hex(target),
            "type": reference.get("type"),
            "from_function": reference.get("from_function_name") or reference.get("from_function"),
            "instruction": reference.get("instruction"),
        }
        by_start[gap.start]["evidence"].append(row)
        by_start[gap.start]["proposed_ranges"].append({
            "start": _hex(target),
            "end": _hex(target),
            "basis": "direct_reference",
        })

    for kind, explicit in (("animation", animation_path), ("movement", movement_path)):
        for stream in _load_streams(root, kind, explicit):
            for gap in gaps:
                if stream["end"] < gap.start or stream["start"] > gap.end:
                    continue
                row = {
                    "kind": "decoded_stream",
                    "stream_kind": kind,
                    "name": stream["name"],
                    "entry": _hex(stream["entry"]),
                    "decoded_range": [_hex(stream["start"]), _hex(stream["end"])],
                    "bytes_decoded": stream["bytes_decoded"],
                    "stopped_reason": stream["stopped_reason"],
                    "source": stream["source"],
                }
                by_start[gap.start]["evidence"].append(row)
                by_start[gap.start]["proposed_ranges"].append({
                    "start": _hex(max(stream["start"], gap.start)),
                    "end": _hex(min(stream["end"], gap.end)),
                    "basis": f"{kind}_decoder",
                })

    template_evidence = _template_pointer_evidence(layout, rom, gaps)
    for gap_start, rows in template_evidence.items():
        by_start[gap_start]["evidence"].extend(rows)
        for row in rows:
            by_start[gap_start]["proposed_ranges"].append({
                "start": row["target"],
                "end": row["target"],
                "basis": "actor_template_pointer",
            })

    result: list[dict[str, Any]] = []
    for gap in gaps:
        item = by_start[gap.start]
        evidence = item["evidence"]
        direct = [row for row in evidence if row["kind"] == "direct_reference"]
        streams = [row for row in evidence if row["kind"] == "decoded_stream"]
        templates = [row for row in evidence if row["kind"] == "actor_template_pointer"]
        if not evidence:
            continue
        stream_kinds = {row["stream_kind"] for row in streams}
        if "animation" in stream_kinds and "movement" in stream_kinds:
            suggested_class = "ANIMATION_STREAM or MOVEMENT_STREAM"
        elif "animation" in stream_kinds:
            suggested_class = "ANIMATION_STREAM"
        elif "movement" in stream_kinds:
            suggested_class = "MOVEMENT_STREAM"
        elif direct or templates:
            reference_types = {str(row.get("type") or "").upper() for row in direct}
            if "CONDITIONAL_JUMP" in reference_types or "UNCONDITIONAL_JUMP" in reference_types:
                suggested_class = "JUMP_TABLE or POINTER_TABLE"
            elif "DATA" in reference_types and len(direct) >= 8:
                suggested_class = "POINTER_TABLE"
            else:
                suggested_class = "OPAQUE_DATA"
        else:
            suggested_class = "UNKNOWN"
        confidence = "high" if streams or templates else "medium"
        score = (
            len(streams) * 1000
            + len(templates) * 750
            + min(len(direct), 100) * 5
            + len({row.get("from_function") for row in direct}) * 10
            + (25 if gap.size <= 256 else 0)
        )
        reasons = []
        if templates:
            reasons.append("direct_actor_template_pointer")
        if streams:
            reasons.append("decoder_overlap")
        if direct:
            reasons.append("incoming_ghidra_references")
        result.append({
            "rank": 0,
            "score": score,
            "gap": item["gap"],
            "suggested_class": suggested_class,
            "confidence": confidence,
            "evidence_counts": {
                "direct_references": len(direct),
                "decoded_streams": len(streams),
                "actor_template_pointers": len(templates),
            },
            "proposed_ranges": sorted(
                item["proposed_ranges"],
                key=lambda value: (_address(value["start"]), _address(value["end"]), value["basis"]),
            ),
            "reasons": reasons,
            "evidence": sorted(
                evidence,
                key=lambda value: (_address(value.get("to", value.get("entry", value.get("target", "0x0")))), value["kind"]),
            )[:max(0, max_references)],
            "evidence_truncated": max(0, len(evidence) - max(0, max_references)),
        })
    result.sort(
        key=lambda value: (
            -value["score"],
            -value["evidence_counts"]["actor_template_pointers"],
            _address(value["gap"]["start"]),
        )
    )
    for rank, item in enumerate(result, 1):
        item["rank"] = rank
    return result


__all__ = ["build_layout_candidates"]
