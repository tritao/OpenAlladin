from __future__ import annotations

import json
from pathlib import Path

from genie.ghidra.database import AnalysisDatabase
from genie.layout.candidates import build_layout_candidates
from genie.layout.model import Layout, LayoutRange


def _database(root: Path) -> Path:
    database = root / "full-rom"
    database.mkdir()
    (database / "metadata.json").write_text(json.dumps({"rom_size": 0x100}), encoding="utf-8")
    (database / "xrefs.json").write_text(json.dumps({"references": [
        {
            "from": "0x00000080",
            "from_function_name": "SpawnActor",
            "to": "0x00000010",
            "type": "DATA",
        },
    ]}), encoding="utf-8")
    return database


def test_layout_candidates_join_decoder_and_actor_template_pointer_evidence(tmp_path):
    database = _database(tmp_path)
    layout = Layout(
        rom_size=0x100,
        ranges=(
            LayoutRange(0x00, 0x1F, "UNKNOWN", "layout.gap"),
            LayoutRange(0x40, 0x53, "ACTOR_TEMPLATE", "tracked.symbol", "ActorTemplate"),
            LayoutRange(0x54, 0xFF, "UNKNOWN", "layout.gap"),
        ),
    )
    rom = bytearray(0x100)
    rom[0x40 + 0x0C:0x40 + 0x10] = (0x10).to_bytes(4, "big")
    animation = tmp_path / "animation.json"
    animation.write_text(json.dumps({"streams": {
        "CandidateAnimation": {
            "entry": "0x10",
            "bytes_decoded": 4,
            "stopped_reason": "byte_limit",
        },
    }}), encoding="utf-8")

    items = build_layout_candidates(
        AnalysisDatabase(database),
        layout,
        root=tmp_path,
        rom=bytes(rom),
        animation_path=animation,
    )

    assert len(items) == 1
    item = items[0]
    assert item["gap"]["start"] == "0x00000000"
    assert item["suggested_class"] == "ANIMATION_STREAM"
    assert item["confidence"] == "high"
    assert item["evidence_quality"] == "strong"
    assert item["promotion"] == "review"
    assert item["evidence_counts"] == {
        "direct_references": 1,
        "code_backed_references": 1,
        "data_only_references": 0,
        "literal_constants": 0,
        "decoded_streams": 1,
        "vm_probes": 0,
        "boundary_conflicts": 0,
        "actor_template_pointers": 1,
    }
    assert "direct_actor_template_pointer" in item["reasons"]
    assert "decoder_overlap" in item["reasons"]


def test_layout_candidates_identify_dense_pointer_reference_gaps(tmp_path):
    database = _database(tmp_path)
    xrefs = [{
        "from": f"0x{0x80 + index * 2:08X}",
        "to": f"0x{0x10 + index:08X}",
        "type": "DATA",
    } for index in range(8)]
    (database / "xrefs.json").write_text(json.dumps({"references": xrefs}), encoding="utf-8")
    layout = Layout(
        rom_size=0x100,
        ranges=(
            LayoutRange(0x00, 0x1F, "UNKNOWN", "layout.gap"),
            LayoutRange(0x20, 0xFF, "CODE", "test"),
        ),
    )

    items = build_layout_candidates(AnalysisDatabase(database), layout, root=tmp_path)

    assert len(items) == 1
    assert items[0]["suggested_class"] == "UNKNOWN"
    assert items[0]["confidence"] == "low"
    assert items[0]["evidence_quality"] == "weak"
    assert items[0]["promotion"] == "do_not_promote"
    assert items[0]["evidence_counts"]["code_backed_references"] == 0
    assert items[0]["evidence_counts"]["data_only_references"] == 8
    assert items[0]["evidence_counts"]["literal_constants"] == 0
    assert "data_only_refs_do_not_identify_format" in items[0]["reasons"]


def test_layout_candidates_do_not_promote_immediate_literal_xrefs(tmp_path):
    database = _database(tmp_path)
    (database / "xrefs.json").write_text(json.dumps({"references": [
        {
            "from": "0x00000080",
            "from_function_name": "SpawnActor",
            "to": "0x00000010",
            "type": "DATA",
            "instruction": "move.w #0x10,(0x1e,A5)",
        },
    ]}), encoding="utf-8")
    layout = Layout(
        rom_size=0x100,
        ranges=(
            LayoutRange(0x00, 0x1F, "UNKNOWN", "layout.gap"),
            LayoutRange(0x20, 0xFF, "CODE", "test"),
        ),
    )

    items = build_layout_candidates(AnalysisDatabase(database), layout, root=tmp_path)

    assert items[0]["suggested_class"] == "UNKNOWN"
    assert items[0]["evidence_quality"] == "weak"
    assert items[0]["evidence_counts"]["literal_constants"] == 1
    assert "literal_constants" in items[0]["reasons"]


def test_layout_candidates_can_hide_weak_data_only_evidence(tmp_path):
    database = _database(tmp_path)
    (database / "xrefs.json").write_text(json.dumps({"references": [
        {
            "from": f"0x{0x80 + index * 2:08X}",
            "to": f"0x{0x10 + index:08X}",
            "type": "DATA",
        }
        for index in range(8)
    ]}), encoding="utf-8")
    layout = Layout(
        rom_size=0x100,
        ranges=(
            LayoutRange(0x00, 0x1F, "UNKNOWN", "layout.gap"),
            LayoutRange(0x20, 0xFF, "CODE", "test"),
        ),
    )

    items = build_layout_candidates(AnalysisDatabase(database), layout, root=tmp_path, strong_only=True)

    assert items == []


def test_layout_candidates_rank_code_backed_references_above_data_only_hits(tmp_path):
    database = _database(tmp_path)
    xrefs = [
        {
            "from": f"0x{0x80 + index * 2:08X}",
            "to": f"0x{0x10 + index:08X}",
            "type": "DATA",
        }
        for index in range(8)
    ]
    xrefs.extend(
        {
            "from": f"0x{0xA0 + index * 2:08X}",
            "from_function_name": "DecodeResource",
            "to": f"0x{0x50 + index:08X}",
            "type": "DATA",
            "instruction": "move.l",
        }
        for index in range(8)
    )
    (database / "xrefs.json").write_text(json.dumps({"references": xrefs}), encoding="utf-8")
    layout = Layout(
        rom_size=0x100,
        ranges=(
            LayoutRange(0x00, 0x1F, "UNKNOWN", "layout.gap"),
            LayoutRange(0x20, 0x3F, "CODE", "test"),
            LayoutRange(0x40, 0x5F, "UNKNOWN", "layout.gap"),
            LayoutRange(0x60, 0xFF, "CODE", "test"),
        ),
    )

    items = build_layout_candidates(AnalysisDatabase(database), layout, root=tmp_path)

    assert [item["gap"]["start"] for item in items] == [
        "0x00000040",
        "0x00000000",
    ]
    assert items[0]["score"] > items[1]["score"]
    assert items[0]["suggested_class"] == "POINTER_TABLE"
    assert items[0]["evidence_quality"] == "medium"
    assert items[0]["evidence_counts"]["code_backed_references"] == 8
    assert items[1]["evidence_counts"]["data_only_references"] == 8
    assert "data_only_refs_do_not_identify_format" in items[1]["reasons"]
