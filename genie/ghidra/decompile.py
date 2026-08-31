"""On-demand cached single-function Ghidra decompilation."""

from __future__ import annotations

import json
import os
import re
import subprocess
from pathlib import Path
from typing import Any

from genie.common import ROOT, load_yaml, parse_int, write_json
from genie.symbols import SymbolStore

from .database import AnalysisDatabase


_AUTO_SYMBOL_RE = re.compile(
    r"(?<![A-Za-z0-9_])(?P<token>_?(?:PTR_DAT|FUN|DAT|UNK)_[0-9A-Fa-f]{6,8})(?![A-Za-z0-9_])"
)


def canonicalize_pseudocode(text: str, symbols: SymbolStore) -> str:
    """Replace known Ghidra auto-symbol tokens with canonical names.

    Ghidra's decompiler can retain ``FUN_*``/``DAT_*`` spellings in a
    function body even after the project has received the tracked symbol
    database.  The generated instruction and analysis databases already use
    canonical names, so keeping a second naming dialect in cached pseudocode
    makes semantic review unnecessarily noisy.  Unknown addresses are left
    untouched; an unproven name must never be fabricated here.
    """

    def replace(match: re.Match[str]) -> str:
        token = match.group("token")
        normalized = token[1:] if token.startswith("_") else token
        kind, raw_address = normalized.rsplit("_", 1)
        address = int(raw_address, 16)
        include_ranges = kind != "FUN"
        symbol = symbols.at(address, include_ranges=include_ranges)
        if symbol is None or (kind == "FUN" and symbol.kind != "function"):
            return token
        if symbol.address == address or symbol.range is None:
            return symbol.name
        offset = address - symbol.address
        return f"{symbol.name}+0x{offset:X}"

    return _AUTO_SYMBOL_RE.sub(replace, text)


def _canonicalize_cached_text(text_path: Path, symbols: SymbolStore) -> bool:
    """Canonicalize an existing cache file, returning whether it changed."""

    text = text_path.read_text(encoding="utf-8")
    canonical = canonicalize_pseudocode(text, symbols)
    if canonical == text:
        return False
    text_path.write_text(canonical, encoding="utf-8")
    return True


def _config() -> dict[str, Any]:
    return dict(load_yaml(ROOT / "re/config/ghidra.yml") or {})


def _cache_paths(database: AnalysisDatabase, address: int, cache_dir: Path | None) -> tuple[Path, Path, Path]:
    directory = Path(cache_dir) if cache_dir is not None else database.root / "decompile"
    stem = f"{address:08X}"
    return directory / f"{stem}.txt", directory / f"{stem}.json", directory / f"{stem}.request.json"


def _project(config: dict[str, Any], project_dir: Path | None) -> tuple[Path, str]:
    values = config.get("ghidra", {})
    directory = (project_dir or ROOT / values["project_dir"]).resolve()
    name = str(values["project_name"])
    if not (directory / f"{name}.gpr").exists() and not (directory / f"{name}.rep").exists():
        raise FileNotFoundError(f"Ghidra project not found: {directory}; run `genie ghidra scan`")
    return directory, name


def decompile_function(
    address: int,
    *,
    database: AnalysisDatabase,
    cache_dir: Path | None = None,
    project_dir: Path | None = None,
    force: bool = False,
) -> dict[str, Any]:
    """Decompile the containing function and cache its pseudocode."""

    address = parse_int(address)
    symbols = SymbolStore(root=ROOT)
    function = database.function(address)
    if function is None:
        raise ValueError(f"no function contains 0x{address:08X}")
    entry = parse_int(function["address"])
    text_path, report_path, request_path = _cache_paths(database, entry, cache_dir)
    if text_path.is_file() and not force:
        _canonicalize_cached_text(text_path, symbols)
        return {
            "status": "cached",
            "address": f"0x{entry:08X}",
            "name": function.get("name"),
            "path": str(text_path),
            "report": str(report_path) if report_path.is_file() else None,
        }

    config = _config()
    project, project_name = _project(config, project_dir)
    install = ROOT / config["ghidra"]["install_dir"]
    headless = install / "support" / ("pyghidraRun.bat" if os.name == "nt" else "pyghidraRun")
    if not headless.is_file():
        raise FileNotFoundError(f"Ghidra launcher not found: {headless}; run `genie ghidra setup`")

    text_path.parent.mkdir(parents=True, exist_ok=True)
    request = {
        "format": "openaladdin-targeted-decompile-request-v1",
        "rom": database.metadata.get("rom"),
        "focus": {"address": f"0x{entry:08X}", "source": "genie ghidra decompile"},
        "targets": [{"address": f"0x{entry:08X}", "name": function.get("name")}],
    }
    write_json(request_path, request)
    command = [
        str(headless),
        "-H",
        str(project),
        project_name,
        "-process",
        "-readOnly",
        "-scriptPath",
        str(ROOT / "re/ghidra/scripts"),
        "-postScript",
        "ExportTargetedDecompile.py",
        str(request_path),
        str(report_path),
    ]
    environment = os.environ.copy()
    environment["GHIDRA_INSTALL_DIR"] = str(install)
    environment["OPENALADDIN_ROOT"] = str(ROOT)
    venv_bin = ROOT / ".tools/venv/bin"
    if venv_bin.is_dir():
        environment["PATH"] = str(venv_bin) + os.pathsep + environment.get("PATH", "")
    try:
        subprocess.run(command, cwd=ROOT, env=environment, check=True)
    except subprocess.CalledProcessError as error:
        raise RuntimeError(f"Ghidra decompile failed with status {error.returncode}") from error

    report = json.loads(report_path.read_text(encoding="utf-8"))
    rows = [row for row in report.get("targets", []) if row.get("address") == f"0x{entry:06X}"]
    if not rows:
        rows = [row for row in report.get("targets", []) if parse_int(row.get("address", -1)) == entry]
    if not rows:
        raise RuntimeError(f"Ghidra decompile report did not contain 0x{entry:08X}")
    row = rows[0]
    status = str(row.get("status", "decompile_failed"))
    pseudocode = row.get("c")
    if status != "decompiled" or not isinstance(pseudocode, str):
        raise RuntimeError(row.get("error") or f"Ghidra could not decompile 0x{entry:08X}")
    pseudocode = canonicalize_pseudocode(pseudocode, symbols)
    text_path.write_text(
        "\n".join([
            "/* Generated by `genie ghidra decompile`; edit tracked knowledge instead. */",
            f"/* address: 0x{entry:08X} */",
            f"/* function: {function.get('name')} */",
            "",
            pseudocode.rstrip(),
            "",
        ]),
        encoding="utf-8",
    )
    return {
        "status": "decompiled",
        "address": f"0x{entry:08X}",
        "name": function.get("name"),
        "path": str(text_path),
        "report": str(report_path),
    }


def _run_targeted_request(
    database: AnalysisDatabase,
    request_path: Path,
    report_path: Path,
    *,
    project_dir: Path | None,
) -> dict[str, Any]:
    """Run one Ghidra process for a request containing one or more targets."""

    config = _config()
    project, project_name = _project(config, project_dir)
    install = ROOT / config["ghidra"]["install_dir"]
    headless = install / "support" / ("pyghidraRun.bat" if os.name == "nt" else "pyghidraRun")
    if not headless.is_file():
        raise FileNotFoundError(f"Ghidra launcher not found: {headless}; run `genie ghidra setup`")

    command = [
        str(headless),
        "-H",
        str(project),
        project_name,
        "-process",
        "-readOnly",
        "-scriptPath",
        str(ROOT / "re/ghidra/scripts"),
        "-postScript",
        "ExportTargetedDecompile.py",
        str(request_path),
        str(report_path),
    ]
    environment = os.environ.copy()
    environment["GHIDRA_INSTALL_DIR"] = str(install)
    environment["OPENALADDIN_ROOT"] = str(ROOT)
    venv_bin = ROOT / ".tools/venv/bin"
    if venv_bin.is_dir():
        environment["PATH"] = str(venv_bin) + os.pathsep + environment.get("PATH", "")
    try:
        subprocess.run(command, cwd=ROOT, env=environment, check=True)
    except subprocess.CalledProcessError as error:
        raise RuntimeError(f"Ghidra decompile failed with status {error.returncode}") from error
    return json.loads(report_path.read_text(encoding="utf-8"))


def _write_decompile_text(
    text_path: Path,
    *,
    entry: int,
    name: str | None,
    pseudocode: str,
    symbols: SymbolStore,
) -> None:
    pseudocode = canonicalize_pseudocode(pseudocode, symbols)
    text_path.write_text(
        "\n".join([
            "/* Generated by `genie ghidra decompile`; edit tracked knowledge instead. */",
            f"/* address: 0x{entry:08X} */",
            f"/* function: {name} */",
            "",
            pseudocode.rstrip(),
            "",
        ]),
        encoding="utf-8",
    )


def decompile_functions(
    addresses: list[int],
    *,
    database: AnalysisDatabase,
    cache_dir: Path | None = None,
    project_dir: Path | None = None,
    force: bool = False,
) -> list[dict[str, Any]]:
    """Decompile several containing functions with one Ghidra launch.

    This is intentionally a batch companion to :func:`decompile_function`.
    The semantic-review queue is small, but launching Ghidra once per entry
    makes review unnecessarily slow and serializes otherwise independent
    pseudocode requests.  Each function still gets the same stable per-entry
    ``.txt``/``.json`` cache files as the single-function command.
    """

    if not addresses:
        return []
    symbols = SymbolStore(root=ROOT)
    directory = Path(cache_dir) if cache_dir is not None else database.root / "decompile"
    requested: list[dict[str, Any]] = []
    seen: set[int] = set()
    for address in addresses:
        address = parse_int(address)
        function = database.function(address)
        if function is None:
            raise ValueError(f"no function contains 0x{address:08X}")
        entry = parse_int(function["address"])
        if entry in seen:
            continue
        seen.add(entry)
        text_path, report_path, _ = _cache_paths(database, entry, directory)
        requested.append({
            "entry": entry,
            "function": function,
            "text_path": text_path,
            "report_path": report_path,
        })

    results: dict[int, dict[str, Any]] = {}
    pending = []
    for item in requested:
        entry = item["entry"]
        text_path = item["text_path"]
        if text_path.is_file() and not force:
            _canonicalize_cached_text(text_path, symbols)
            results[entry] = {
                "status": "cached",
                "address": f"0x{entry:08X}",
                "name": item["function"].get("name"),
                "path": str(text_path),
                "report": str(item["report_path"]) if item["report_path"].is_file() else None,
            }
        else:
            pending.append(item)
    if pending:
        directory.mkdir(parents=True, exist_ok=True)
        request_path = directory / "batch.request.json"
        report_path = directory / "batch.json"
        request = {
            "format": "openaladdin-targeted-decompile-request-v1",
            "rom": database.metadata.get("rom"),
            "focus": {"source": "genie ghidra decompile --review"},
            "targets": [
                {
                    "address": f"0x{item['entry']:08X}",
                    "name": item["function"].get("name"),
                }
                for item in pending
            ],
        }
        write_json(request_path, request)
        report = _run_targeted_request(
            database,
            request_path,
            report_path,
            project_dir=project_dir,
        )
        rows = {}
        for row in report.get("targets", []):
            try:
                rows[parse_int(row.get("address"))] = row
            except (TypeError, ValueError):
                continue
        for item in pending:
            entry = item["entry"]
            row = rows.get(entry)
            if row is None:
                raise RuntimeError(f"Ghidra decompile report did not contain 0x{entry:08X}")
            status = str(row.get("status", "decompile_failed"))
            pseudocode = row.get("c")
            if status != "decompiled" or not isinstance(pseudocode, str):
                raise RuntimeError(row.get("error") or f"Ghidra could not decompile 0x{entry:08X}")
            _write_decompile_text(
                item["text_path"],
                entry=entry,
                name=item["function"].get("name"),
                pseudocode=pseudocode,
                symbols=symbols,
            )
            write_json(
                item["report_path"],
                {
                    "format": "openaladdin-targeted-decompile-v1",
                    "rom": database.metadata.get("rom"),
                    "targets": [row],
                },
            )
            results[entry] = {
                "status": "decompiled",
                "address": f"0x{entry:08X}",
                "name": item["function"].get("name"),
                "path": str(item["text_path"]),
                "report": str(report_path),
            }
    return [results[item["entry"]] for item in requested]


__all__ = ["canonicalize_pseudocode", "decompile_function", "decompile_functions"]
