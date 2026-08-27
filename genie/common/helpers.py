#!/usr/bin/env python3
"""Small, dependency-light helpers shared by the OpenAladdin tools."""

from __future__ import annotations

import hashlib
import json
import re
import zlib
from pathlib import Path
from typing import Any, Iterable

ROOT = Path(__file__).resolve().parents[2]


def _strip_comment(value: str) -> str:
    quoted = None
    escaped = False
    for index, char in enumerate(value):
        if escaped:
            escaped = False
            continue
        if char == "\\" and quoted:
            escaped = True
            continue
        if char in "\"'":
            if quoted == char:
                quoted = None
            elif quoted is None:
                quoted = char
            continue
        if char == "#" and quoted is None and (index == 0 or value[index - 1].isspace()):
            return value[:index].rstrip()
    return value.rstrip()


def _scalar(value: str) -> Any:
    value = value.strip()
    if not value:
        return None
    if value in ("{}", "{ }"):
        return {}
    if value in ("[]", "[ ]"):
        return []
    if value[0:1] in ("\"", "'") and value[-1:] == value[0]:
        return value[1:-1] if value[0] == "'" else json.loads(value)
    if value.lower() in ("true", "false"):
        return value.lower() == "true"
    if value.lower() in ("null", "~"):
        return None
    if value.startswith("[") and value.endswith("]"):
        return [_scalar(part) for part in value[1:-1].split(",") if part.strip()]
    if value.startswith("{") and value.endswith("}"):
        result = {}
        for part in value[1:-1].split(","):
            if ":" in part:
                key, item = part.split(":", 1)
                result[key.strip().strip("\"'")] = _scalar(item)
        return result
    try:
        return int(value, 0)
    except ValueError:
        return value


def _simple_yaml(text: str) -> Any:
    """Parse the small YAML subset used by this repository.

    PyYAML is preferred when available. This fallback keeps a fresh clone
    usable with the Python standard library alone.
    """

    lines = []
    for raw in text.splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        lines.append((indent, _strip_comment(raw[indent:])))
    lines = [(indent, line) for indent, line in lines if line]

    def block(pos: int, indent: int):
        if pos >= len(lines) or lines[pos][0] < indent:
            return {}, pos
        is_list = lines[pos][0] == indent and lines[pos][1].startswith("-")
        result = [] if is_list else {}
        while pos < len(lines):
            current_indent, content = lines[pos]
            if current_indent < indent:
                break
            if current_indent > indent:
                raise ValueError(f"unsupported YAML indentation near: {content}")
            if is_list:
                if not content.startswith("-"):
                    break
                item = content[1:].strip()
                pos += 1
                if not item:
                    if pos < len(lines) and lines[pos][0] > indent:
                        value, pos = block(pos, lines[pos][0])
                    else:
                        value = None
                elif ":" in item and not item.startswith(("\"", "'")):
                    key, raw_value = item.split(":", 1)
                    value = {key.strip(): _scalar(raw_value) if raw_value.strip() else None}
                    if pos < len(lines) and lines[pos][0] > indent:
                        child, pos = block(pos, lines[pos][0])
                        if isinstance(child, dict):
                            value.update(child)
                else:
                    value = _scalar(item)
                result.append(value)
                continue
            if ":" not in content:
                raise ValueError(f"expected key/value near: {content}")
            key, raw_value = content.split(":", 1)
            key = key.strip().strip("\"'")
            pos += 1
            if raw_value.strip():
                result[key] = _scalar(raw_value)
            elif pos < len(lines) and lines[pos][0] > indent:
                result[key], pos = block(pos, lines[pos][0])
            else:
                result[key] = None
        return result, pos

    return block(0, lines[0][0])[0] if lines else {}


def load_yaml(path: Path) -> Any:
    text = path.read_text(encoding="utf-8")
    try:
        import yaml  # type: ignore
    except ImportError:
        return _simple_yaml(text)
    return yaml.safe_load(text)


def parse_int(value: Any) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    return int(str(value).strip(), 0)


def hashes(path: Path) -> dict[str, Any]:
    sha1 = hashlib.sha1()
    sha256 = hashlib.sha256()
    crc = 0
    size = 0
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            size += len(chunk)
            sha1.update(chunk)
            sha256.update(chunk)
            crc = zlib.crc32(chunk, crc)
    return {
        "size": size,
        "crc32": f"{crc & 0xFFFFFFFF:08X}",
        "sha1": sha1.hexdigest().upper(),
        "sha256": sha256.hexdigest(),
    }


def rom_entries() -> tuple[str, dict[str, Any], dict[str, Any]]:
    document = load_yaml(ROOT / "re/config/roms.yml") or {}
    default = document.get("default", "aladdin_local")
    entries = {key: value for key, value in document.items() if key != "default"}
    if default not in entries:
        raise ValueError(f"default ROM entry {default!r} is not defined")
    return default, entries[default], entries


def normalize_symbols() -> list[dict[str, Any]]:
    result = []
    for category in ("functions", "ram", "data"):
        document = load_yaml(ROOT / f"re/symbols/{category}.yml") or {}
        if not isinstance(document, dict):
            raise ValueError(f"re/symbols/{category}.yml must contain an address map")
        for raw_address, raw_entry in document.items():
            if isinstance(raw_address, int):
                address = raw_address
            elif str(raw_address).lower().startswith("0x"):
                address = parse_int(raw_address)
            else:
                continue
            entry = dict(raw_entry or {})
            entry.update(
                address=address,
                kind="function" if category == "functions" else "label",
                category=category,
            )
            if "name" not in entry:
                raise ValueError(f"{category} symbol {raw_address} has no name")
            result.append(entry)
    return sorted(result, key=lambda item: item["address"])


def normalize_types() -> list[dict[str, Any]]:
    result = []
    for path in sorted((ROOT / "re/types").glob("*.yml")):
        entry = load_yaml(path) or {}
        if entry:
            entry["source"] = str(path.relative_to(ROOT))
            result.append(entry)
    return result


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_mame_symbols(path: Path, symbols: Iterable[dict[str, Any]]) -> None:
    lines = ["-- Generated by genie/ghidra/import_rom.py; edit re/symbols/*.yml instead.", "symbols = {"]
    for symbol in symbols:
        name = str(symbol["name"])
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            key = name
        else:
            key = f"[{json.dumps(name)}]"
        lines.append(f"    {key} = 0x{symbol['address']:06X},")
    lines.extend(["}", "return symbols", ""])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")
