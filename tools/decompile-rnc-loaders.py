#!/usr/bin/env python3
"""Run a focused Ghidra decompile pass over unresolved RNC loader clusters."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Any

from common import ROOT, load_yaml, write_json


def _address(value: str | int) -> int:
    return int(str(value), 0)


def _hex(value: int) -> str:
    return f"0x{value:06X}"


def _default_rom() -> Path:
    for candidate in (ROOT / "Disneys_Aladdin_U_p1.bin", ROOT / "rom/aladdin-usa.bin"):
        if candidate.exists():
            return candidate
    return ROOT / "Disneys_Aladdin_U_p1.bin"


def _load_json(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise SystemExit(f"required report not found: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def build_targets(
    loader: dict[str, Any],
    runtime: dict[str, Any],
    include_all: bool,
    extra_addresses: list[str],
) -> list[dict[str, Any]]:
    observed = {
        event["source"]
        for event in runtime.get("events", [])
        if event.get("block")
    }
    groups: dict[str, dict[str, Any]] = {}
    for call in loader.get("calls", []):
        block = call.get("block")
        function = call.get("function")
        if not block or not function:
            continue
        offset = block["offset"]
        if not include_all and offset in observed:
            continue
        address = function["address"]
        group = groups.setdefault(address, {
            "address": address,
            "name": function.get("name"),
            "call_sites": [],
            "blocks": {},
            "destinations": set(),
        })
        group["call_sites"].append(call["call_address"])
        group["blocks"].setdefault(offset, {
            "offset": offset,
            "observed": offset in observed,
            "unpacked_bytes": block.get("unpacked_bytes"),
            "storage_family": block.get("storage_family"),
        })
        if call.get("destination"):
            group["destinations"].add(call["destination"]["target"])

    targets = [{
        "address": _hex(0x1B3416),
        "name": "RNC_To_VDP_Loader",
        "kind": "confirmed_loader",
        "call_sites": [],
        "blocks": [],
        "destinations": [],
    }]
    for address in sorted(groups, key=lambda value: _address(value)):
        group = groups[address]
        targets.append({
            "address": address,
            "name": group["name"],
            "kind": "unresolved_loader_cluster",
            "call_sites": sorted(set(group["call_sites"]), key=lambda value: _address(value)),
            "blocks": [group["blocks"][offset] for offset in sorted(group["blocks"], key=_address)],
            "destinations": sorted(group["destinations"], key=_address),
        })
    known_addresses = {target["address"] for target in targets}
    for raw_address in extra_addresses:
        address = _hex(_address(raw_address))
        if address in known_addresses:
            continue
        targets.append({
            "address": address,
            "name": None,
            "kind": "manual_context",
            "call_sites": [],
            "blocks": [],
            "destinations": [],
        })
        known_addresses.add(address)
    return targets


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", nargs="?", type=Path, default=_default_rom())
    parser.add_argument(
        "--loader-analysis",
        type=Path,
        default=ROOT / "build/assets/rnc/loader_analysis.json",
    )
    parser.add_argument(
        "--runtime-load-trace",
        type=Path,
        default=ROOT / "build/re/rnc-capture-matrix/combined/rnc_loads.json",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "build/re/rnc_targeted_decompile.json",
    )
    parser.add_argument(
        "--all-loader-functions",
        action="store_true",
        help="decompile every loader-containing function instead of only unobserved clusters",
    )
    parser.add_argument(
        "--extra-address",
        action="append",
        default=[],
        help="also decompile this function address; repeatable for scene-dispatch context",
    )
    parser.add_argument(
        "--project-dir",
        type=Path,
        help="Ghidra project directory; defaults to re/config/ghidra.yml",
    )
    args = parser.parse_args()

    rom = args.rom.resolve()
    loader_path = args.loader_analysis.resolve()
    runtime_path = args.runtime_load_trace.resolve()
    loader = _load_json(loader_path)
    runtime = _load_json(runtime_path)
    targets = build_targets(loader, runtime, args.all_loader_functions, args.extra_address)
    request = {
        "format": "openaladdin-targeted-decompile-request-v1",
        "rom": str(rom),
        "focus": {
            "loader_analysis": str(loader_path),
            "runtime_load_trace": str(runtime_path),
            "observed_block_count": runtime.get("summary", {}).get("known_block_count", 0),
            "target_count": len(targets),
        },
        "targets": targets,
    }
    request_path = args.output.resolve().with_suffix(".request.json")
    write_json(request_path, request)

    config = load_yaml(ROOT / "re/config/ghidra.yml")
    project_dir = (args.project_dir or ROOT / config["ghidra"]["project_dir"]).resolve()
    project_name = config["ghidra"]["project_name"]
    headless = ROOT / config["ghidra"]["install_dir"] / "support/pyghidraRun"
    if not headless.is_file():
        raise SystemExit(f"Ghidra launcher not found: {headless}; run tools/setup-ghidra.py")
    if not (project_dir / f"{project_name}.gpr").exists() and not (project_dir / f"{project_name}.rep").exists():
        raise SystemExit(f"Ghidra project not found: {project_dir}; run tools/import-rom.py first")

    script_path = ROOT / "re/ghidra/scripts"
    command = [
        str(headless),
        "-H",
        str(project_dir),
        project_name,
        "-process",
        "-readOnly",
        "-scriptPath",
        str(script_path),
        "-postScript",
        "ExportTargetedDecompile.py",
        str(request_path),
        str(args.output.resolve()),
    ]
    environment = os.environ.copy()
    environment["GHIDRA_INSTALL_DIR"] = str(ROOT / config["ghidra"]["install_dir"])
    environment["OPENALADDIN_ROOT"] = str(ROOT)
    venv_bin = ROOT / ".tools/venv/bin"
    if venv_bin.is_dir():
        environment["PATH"] = str(venv_bin) + os.pathsep + environment.get("PATH", "")
    print("Target functions:", ", ".join(target["address"] for target in targets))
    print("Running targeted Ghidra decompile")
    subprocess.run(command, cwd=ROOT, env=environment, check=True)
    report = _load_json(args.output.resolve())
    print(f"decompiled functions: {sum(row.get('status') == 'decompiled' for row in report['targets'])}")
    print(f"decompile report: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
