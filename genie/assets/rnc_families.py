"""Evidence report for grouping and investigating unassigned RNC assets."""

from __future__ import annotations

import bisect
import csv
import json
from pathlib import Path
from typing import Any, Iterable


CODE_CLUSTER_GAP = 0x100
CONTEXT_BEFORE_BYTES = 8
CONTEXT_AFTER_BYTES = 24


def _address(value: Any) -> int:
    return int(str(value), 16)


def _hex_address(value: int) -> str:
    return f"0x{value:06X}"


def _hex_bytes(data: bytes) -> str:
    return data.hex().upper()


def _function_index(path: Path | None) -> tuple[list[int], dict[int, str]]:
    if path is None or not path.is_file():
        return [], {}
    starts: list[int] = []
    names: dict[int, str] = {}
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            try:
                address = _address(row["address"])
            except (KeyError, TypeError, ValueError):
                continue
            starts.append(address)
            names[address] = row.get("name") or f"FUN_{address:06x}"
    starts = sorted(set(starts))
    return starts, names


def _containing_function(address: int, starts: list[int], names: dict[int, str]) -> dict[str, str] | None:
    if not starts:
        return None
    index = bisect.bisect_right(starts, address) - 1
    if index < 0:
        return None
    start = starts[index]
    return {"address": _hex_address(start), "name": names[start]}


def _instruction(data: bytes, address: int) -> dict[str, Any]:
    """Decode the small set of 68000 forms used by the asset-load sites.

    Unknown words are deliberately reported as ``dc.w`` and stop the local
    window. This keeps the report honest instead of presenting a partial
    disassembler as authoritative.
    """

    if address < 0 or address + 2 > len(data):
        return {"address": _hex_address(max(address, 0)), "mnemonic": "outside_rom", "size": 0}
    opcode = int.from_bytes(data[address:address + 2], "big")
    result: dict[str, Any] = {
        "address": _hex_address(address),
        "opcode": f"0x{opcode:04X}",
    }

    if (opcode & 0xF1FF) == 0x41F9 and address + 6 <= len(data):
        register = (opcode >> 9) & 7
        target = int.from_bytes(data[address + 2:address + 6], "big")
        result.update({
            "mnemonic": "lea.l",
            "operands": f"{_hex_address(target)},A{register}",
            "target": _hex_address(target),
            "size": 6,
        })
    elif (opcode & 0xF1FF) == 0x41F8 and address + 4 <= len(data):
        register = (opcode >> 9) & 7
        target = int.from_bytes(data[address + 2:address + 4], "big", signed=True) & 0xFFFFFFFF
        result.update({
            "mnemonic": "lea.w",
            "operands": f"0x{target:08X},A{register}",
            "size": 4,
        })
    elif opcode in (0x4E75, 0x4E71, 0x4E77):
        result.update({"mnemonic": {0x4E75: "rts", 0x4E71: "nop", 0x4E77: "rtr"}[opcode], "size": 2})
    elif opcode & 0xFF00 == 0x6100:
        displacement = opcode & 0xFF
        if displacement == 0 and address + 4 <= len(data):
            displacement = int.from_bytes(data[address + 2:address + 4], "big", signed=True)
            size = 4
        else:
            displacement = displacement if displacement < 0x80 else displacement - 0x100
            size = 2
        # 68000 word-displacement branches use the extension-word address as
        # the PC-relative base; the instruction's total size is four bytes.
        result.update({"mnemonic": "bsr", "target": _hex_address(address + 2 + displacement), "size": size})
    elif opcode & 0xFF00 == 0x6000:
        displacement = opcode & 0xFF
        if displacement == 0 and address + 4 <= len(data):
            displacement = int.from_bytes(data[address + 2:address + 4], "big", signed=True)
            size = 4
        else:
            displacement = displacement if displacement < 0x80 else displacement - 0x100
            size = 2
        result.update({"mnemonic": "branch", "target": _hex_address(address + 2 + displacement), "size": size})
    elif opcode == 0x23FC and address + 10 <= len(data):
        value = int.from_bytes(data[address + 2:address + 6], "big")
        target = int.from_bytes(data[address + 6:address + 10], "big")
        result.update({"mnemonic": "move.l", "operands": f"#$%08X,%s" % (value, _hex_address(target)), "size": 10})
    elif opcode == 0x33FC and address + 8 <= len(data):
        value = int.from_bytes(data[address + 2:address + 4], "big")
        target = int.from_bytes(data[address + 4:address + 8], "big")
        result.update({"mnemonic": "move.w", "operands": f"#$%04X,%s" % (value, _hex_address(target)), "size": 8})
    elif opcode == 0x13FC and address + 8 <= len(data):
        value = data[address + 3]
        target = int.from_bytes(data[address + 4:address + 8], "big")
        result.update({"mnemonic": "move.b", "operands": f"#$%02X,%s" % (value, _hex_address(target)), "size": 8})
    else:
        result.update({"mnemonic": "dc.w", "operands": f"${opcode:04X}", "size": 0})

    size = int(result["size"])
    result["bytes"] = _hex_bytes(data[address:address + size if size else address + 2])
    return result


def _instruction_window(data: bytes, address: int, count: int = 4) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    current = address
    for _ in range(count):
        item = _instruction(data, current)
        result.append(item)
        size = int(item.get("size", 0))
        if size <= 0:
            break
        current += size
        if current >= len(data):
            break
    return result


def _reference_detail(
    data: bytes,
    reference: dict[str, Any],
    block: dict[str, Any],
    classification: dict[str, Any],
    starts: list[int],
    names: dict[int, str],
) -> dict[str, Any]:
    instruction_address = _address(reference["instruction_address"])
    known = _instruction(data, instruction_address)
    before = max(0, instruction_address - CONTEXT_BEFORE_BYTES)
    after = min(len(data), instruction_address + CONTEXT_AFTER_BYTES)
    return {
        "instruction_address": reference["instruction_address"],
        "reference_address": reference["reference_address"],
        "target_offset": reference["target_offset"],
        "context": reference.get("context"),
        "opcode": reference.get("opcode"),
        "function": _containing_function(instruction_address, starts, names),
        "instruction": known,
        "following_instructions": _instruction_window(
            data, instruction_address + max(2, int(known.get("size", 0))), 4
        ),
        "surrounding_bytes": {
            "start": _hex_address(before),
            "end": _hex_address(after),
            "hex": _hex_bytes(data[before:after]),
        },
        "asset": {
            "offset": block["offset"],
            "packed_bytes": block.get("packed_bytes"),
            "unpacked_bytes": block.get("unpacked_bytes"),
            "classification": classification.get("classification"),
            "preview": (classification.get("rendered") or {}).get("file"),
        },
    }


def _group_references(references: Iterable[dict[str, Any]], gap: int) -> list[list[dict[str, Any]]]:
    groups: list[list[dict[str, Any]]] = []
    current: list[dict[str, Any]] = []
    previous: int | None = None
    for reference in sorted(references, key=lambda row: _address(row["instruction_address"])):
        address = _address(reference["instruction_address"])
        if current and previous is not None and address - previous > gap:
            groups.append(current)
            current = []
        current.append(reference)
        previous = address
    if current:
        groups.append(current)
    return groups


def analyze_rnc_families(
    rom_data: bytes,
    corpus_root: Path,
    rom_identity: dict[str, Any] | None = None,
    functions_path: Path | None = None,
    code_cluster_gap: int = CODE_CLUSTER_GAP,
) -> dict[str, Any]:
    """Build a review-oriented report for unassigned RNC graphics.

    The report keeps three independent kinds of evidence visible: contiguous
    compressed storage families, ROM pointer/code clusters, and the existing
    rendered preview for each decompressed block. It assigns no canonical
    game meaning.
    """

    corpus_root = corpus_root.resolve()
    manifest = json.loads((corpus_root / "manifest.json").read_text(encoding="utf-8"))
    classification = json.loads((corpus_root / "classification.json").read_text(encoding="utf-8"))
    pointer_report = json.loads((corpus_root / "pointer_references.json").read_text(encoding="utf-8"))
    starts, names = _function_index(functions_path)

    manifest_blocks = {
        block["offset"]: block
        for block in manifest.get("blocks", [])
        if block.get("decoded")
    }
    classified_blocks = {
        block["offset"]: block
        for block in classification.get("blocks", [])
    }
    unassigned_offsets = set(classified_blocks)
    raw_references = [
        reference
        for reference in pointer_report.get("references", [])
        if reference.get("target_unassigned") and reference.get("target_offset") in unassigned_offsets
    ]
    references_by_target: dict[str, list[dict[str, Any]]] = {offset: [] for offset in unassigned_offsets}
    for reference in raw_references:
        references_by_target[reference["target_offset"]].append(reference)

    storage_family_by_block: dict[str, str] = {}
    storage_families: list[dict[str, Any]] = []
    for index, family in enumerate(classification.get("families", []), start=1):
        family_id = f"storage-{index:02d}"
        block_offsets = [offset for offset in family.get("blocks", []) if offset in unassigned_offsets]
        if not block_offsets:
            continue
        for offset in block_offsets:
            storage_family_by_block[offset] = family_id
        blocks = [manifest_blocks[offset] for offset in block_offsets]
        family_references = [reference for offset in block_offsets for reference in references_by_target[offset]]
        storage_families.append({
            "id": family_id,
            "first_offset": family["first_offset"],
            "last_offset": family["last_offset"],
            "block_count": len(blocks),
            "packed_bytes": sum(int(block.get("packed_bytes", 0)) for block in blocks),
            "rom_bytes": sum(int(block.get("total_bytes", 0)) for block in blocks),
            "unpacked_bytes": sum(int(block.get("unpacked_bytes", 0)) for block in blocks),
            "classifications": sorted({classified_blocks[offset].get("classification") for offset in block_offsets}),
            "blocks": block_offsets,
            "reference_count": len(family_references),
            "candidate_status": "unassigned",
            "candidate_role": "unknown_graphics" if all(
                classified_blocks[offset].get("classification") == "genesis_tile_candidate"
                for offset in block_offsets
            ) else "unknown_binary",
        })

    code_cluster_by_reference: dict[tuple[str, str], str] = {}
    code_clusters: list[dict[str, Any]] = []
    for index, group in enumerate(_group_references(raw_references, code_cluster_gap), start=1):
        cluster_id = f"code-{index:02d}"
        details: list[dict[str, Any]] = []
        for reference in group:
            target = reference["target_offset"]
            detail = _reference_detail(
                rom_data,
                reference,
                manifest_blocks[target],
                classified_blocks[target],
                starts,
                names,
            )
            details.append(detail)
            code_cluster_by_reference[(reference["instruction_address"], target)] = cluster_id
        addresses = [_address(reference["instruction_address"]) for reference in group]
        code_clusters.append({
            "id": cluster_id,
            "start": _hex_address(min(addresses)),
            "end": _hex_address(max(addresses)),
            "gap_threshold": _hex_address(code_cluster_gap),
            "reference_count": len(details),
            "target_count": len({detail["target_offset"] for detail in details}),
            "target_offsets": sorted({detail["target_offset"] for detail in details}),
            "contexts": sorted({detail.get("context") for detail in details}),
            "functions": sorted({
                item["name"]
                for detail in details
                if (item := detail.get("function")) is not None
            }),
            "references": details,
        })

    block_rows: list[dict[str, Any]] = []
    for offset in sorted(unassigned_offsets, key=_address):
        corpus_block = manifest_blocks[offset]
        classified_block = classified_blocks[offset]
        block_references = references_by_target[offset]
        cluster_ids = sorted({
            code_cluster_by_reference[(reference["instruction_address"], offset)]
            for reference in block_references
        })
        block_rows.append({
            "offset": offset,
            "storage_family": storage_family_by_block.get(offset),
            "packed_bytes": corpus_block.get("packed_bytes"),
            "unpacked_bytes": corpus_block.get("unpacked_bytes"),
            "classification": classified_block.get("classification"),
            "tile_count": classified_block.get("tile_count"),
            "preview": (classified_block.get("rendered") or {}).get("file"),
            "reference_count": len(block_references),
            "code_clusters": cluster_ids,
            "reference_addresses": [reference["instruction_address"] for reference in block_references],
            "status": "unassigned",
        })

    for family in storage_families:
        family["code_clusters"] = sorted({
            cluster_id
            for offset in family["blocks"]
            for cluster_id in next(
                (row["code_clusters"] for row in block_rows if row["offset"] == offset), []
            )
        })

    report = {
        "format": "openaladdin-rnc-family-analysis-v1",
        "rom": rom_identity or manifest.get("rom", {}),
        "inputs": {
            "corpus": "rnc/manifest.json",
            "classification": "rnc/classification.json",
            "pointers": "rnc/pointer_references.json",
            "functions": "build/re/functions.csv" if functions_path and functions_path.is_file() else None,
        },
        "heuristics": {
            "code_cluster_gap": _hex_address(code_cluster_gap),
            "classification_note": "genesis_tile_candidate remains a size/alignment heuristic; previews use the evidence palette.",
            "pointer_note": "Only references marked unassigned by the corpus are included in the family report.",
        },
        "summary": {
            "unassigned_block_count": len(block_rows),
            "unassigned_unpacked_bytes": sum(int(row.get("unpacked_bytes") or 0) for row in block_rows),
            "storage_family_count": len(storage_families),
            "code_cluster_count": len(code_clusters),
            "reference_count": len(raw_references),
            "tile_candidate_count": sum(row.get("classification") == "genesis_tile_candidate" for row in block_rows),
        },
        "storage_families": storage_families,
        "code_clusters": code_clusters,
        "blocks": block_rows,
    }
    output_path = corpus_root / "family_analysis.json"
    output_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def write_rnc_family_analysis(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
