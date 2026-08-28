"""Fast read-only queries over the generated whole-ROM Ghidra database."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Iterable

from genie.common import ROOT, parse_int


DEFAULT_DATABASE = ROOT / "build/re/full-rom"


def _address(value: Any) -> int:
    return parse_int(value)


def _address_text(value: Any) -> str:
    return f"0x{_address(value):08X}"


class AnalysisDatabase:
    """Lazy reader for the deterministic files emitted by ``ghidra scan``."""

    def __init__(self, root: Path = DEFAULT_DATABASE):
        self.root = Path(root)
        self._cache: dict[str, Any] = {}

    def load(self, filename: str) -> Any:
        if filename not in self._cache:
            path = self.root / filename
            if not path.is_file():
                raise FileNotFoundError(f"analysis database file not found: {path}; run `genie ghidra scan`")
            self._cache[filename] = json.loads(path.read_text(encoding="utf-8"))
        return self._cache[filename]

    @property
    def metadata(self) -> dict[str, Any]:
        return dict(self.load("metadata.json"))

    @property
    def functions(self) -> list[dict[str, Any]]:
        document = self.load("functions.json")
        if isinstance(document, dict):
            document = document.get("functions", [])
        return sorted((dict(item) for item in document), key=lambda item: _address(item["address"]))

    @property
    def instructions(self) -> list[dict[str, Any]]:
        document = self.load("instructions.json")
        if isinstance(document, dict):
            document = document.get("instructions", [])
        return sorted((dict(item) for item in document), key=lambda item: _address(item["address"]))

    def _records(self, filename: str, key: str) -> list[dict[str, Any]]:
        document = self.load(filename)
        values = document.get(key, []) if isinstance(document, dict) else document
        return [dict(item) for item in values]

    def function(self, address: int) -> dict[str, Any] | None:
        address = _address(address)
        for function in self.functions:
            if _address(function["address"]) == address:
                return function
        for function in self.functions:
            start = _address(function.get("start") or function["address"])
            end = _address(function.get("end") or function["address"])
            if start <= address <= end:
                return function
        return None

    def callgraph(self) -> list[dict[str, Any]]:
        return self._records("callgraph.json", "edges")

    def callers(self, address: int) -> list[dict[str, Any]]:
        function = self.function(address)
        target = _address_text(function["address"]) if function else _address_text(address)
        return sorted(
            [edge for edge in self.callgraph() if _address_text(edge.get("to")) == target],
            key=lambda edge: (_address(edge.get("from", 0)), _address(edge.get("site", 0))),
        )

    def callees(self, address: int) -> list[dict[str, Any]]:
        function = self.function(address)
        source = _address_text(function["address"]) if function else _address_text(address)
        return sorted(
            [edge for edge in self.callgraph() if _address_text(edge.get("from")) == source],
            key=lambda edge: (_address(edge.get("to", 0)), _address(edge.get("site", 0))),
        )

    def xrefs(self, address: int) -> list[dict[str, Any]]:
        target = _address(address)
        return sorted(
            [item for item in self._records("xrefs.json", "references") if _address(item["to"]) == target],
            key=lambda item: (_address(item.get("from", 0)), str(item.get("type", ""))),
        )

    def _memory_references(self, filename: str, address: int) -> list[dict[str, Any]]:
        target = _address(address)
        return sorted(
            [item for item in self._records(filename, "references") if _address(item["to"]) == target],
            key=lambda item: (_address(item.get("from", 0)), str(item.get("type", ""))),
        )

    def readers(self, address: int) -> list[dict[str, Any]]:
        return self._memory_references("memory_reads.json", address)

    def writers(self, address: int) -> list[dict[str, Any]]:
        return self._memory_references("memory_writes.json", address)

    def unknown(self) -> list[dict[str, Any]]:
        document = self.load("address_classes.json")
        classes = document.get("classes", []) if isinstance(document, dict) else document
        return sorted(
            [dict(item) for item in classes if str(item.get("class", "")).upper() == "UNKNOWN"],
            key=lambda item: (_address(item["start"]), _address(item["end"])),
        )

    def stats(self) -> dict[str, Any]:
        metadata = self.metadata
        counts = dict(metadata.get("counts", {}))
        if not counts:
            counts = {
                "functions": len(self.functions),
                "instructions": len(self.instructions),
                "callgraph_edges": len(self.callgraph()),
                "xrefs": len(self._records("xrefs.json", "references")),
                "memory_reads": len(self._records("memory_reads.json", "references")),
                "memory_writes": len(self._records("memory_writes.json", "references")),
                "indirect_calls": len(self._records("indirect_calls.json", "references")),
                "jump_tables": len(self._records("jump_tables.json", "tables")),
                "address_classes": len(self.unknown()),
            }
        return {"root": str(self.root), "format": metadata.get("format"), "counts": counts}


def render_records(records: Iterable[dict[str, Any]], *, json_output: bool) -> None:
    values = list(records)
    if json_output:
        print(json.dumps(values, indent=2, sort_keys=True))
        return
    for record in values:
        if "from" in record or "to" in record:
            left = _address_text(record.get("from", record.get("start", 0)))
            right = _address_text(record.get("to", record.get("end", 0)))
            suffix = f" {record.get('type', record.get('class', ''))}".rstrip()
            print(f"{left} -> {right}{suffix}")
        else:
            print(json.dumps(record, sort_keys=True))
