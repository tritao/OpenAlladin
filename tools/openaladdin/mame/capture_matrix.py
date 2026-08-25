#!/usr/bin/env python3
"""Run repeatable MAME scenarios and aggregate runtime asset evidence."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Any

from openaladdin.common import ROOT, load_yaml, write_json


SCENARIO_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]*$")


def _default_rom() -> Path:
    for candidate in (ROOT / "rom/Disneys_Aladdin_U_p1.bin", ROOT / "rom/aladdin-usa.bin"):
        if candidate.exists():
            return candidate
    return ROOT / "rom/Disneys_Aladdin_U_p1.bin"


def _relative(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(ROOT))
    except ValueError:
        return str(path.resolve())


def _bool_env(value: Any, default: bool) -> str:
    if value is None:
        value = default
    return "1" if bool(value) else "0"


def _load_scenarios(config_path: Path, selected: set[str]) -> list[dict[str, Any]]:
    config = load_yaml(config_path) or {}
    defaults = config.get("defaults") or {}
    scenarios = config.get("scenarios") or []
    if not isinstance(scenarios, list) or not scenarios:
        raise ValueError(f"no scenarios found in {config_path}")
    result = []
    for raw in scenarios:
        scenario = dict(defaults)
        scenario.update(raw or {})
        name = str(scenario.get("name", ""))
        if not SCENARIO_NAME.fullmatch(name):
            raise ValueError(f"invalid scenario name: {name!r}")
        if selected and name not in selected:
            continue
        if int(scenario.get("frames", 0)) <= 0:
            raise ValueError(f"scenario {name} must have positive frames")
        if "input" not in scenario:
            raise ValueError(f"scenario {name} has no input schedule")
        result.append(scenario)
    if selected and not result:
        raise ValueError(f"no selected scenarios found: {sorted(selected)}")
    return result


def _run_scenario(
    rom: Path,
    output_root: Path,
    scenario: dict[str, Any],
    parser_path: Path,
    loader_path: Path,
    dry_run: bool,
) -> dict[str, Any]:
    name = scenario["name"]
    trace_dir = (output_root / name).resolve()
    trace_dir.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    for key in (
        "OPENALADDIN_TRACE_DIR",
        "OPENALADDIN_TRACE_FRAMES",
        "OPENALADDIN_INPUT",
        "OPENALADDIN_CAPTURE",
        "OPENALADDIN_CAPTURE_VDP",
        "OPENALADDIN_STATE_OUTPUT",
        "OPENALADDIN_TRACE_RNC_LOADS",
        "OPENALADDIN_TRACE_SCENE_STATES",
        "OPENALADDIN_TRACE_ACTORS",
        "OPENALADDIN_TRACE_ACTOR_INIT",
        "OPENALADDIN_LOAD_STATE",
        "OPENALADDIN_SAVE_FRAME",
        "OPENALADDIN_SNAPSHOT_FRAME",
        "OPENALADDIN_SAVE_NAME",
        "OPENALADDIN_SNAPSHOT_NAME",
    ):
        environment.pop(key, None)
    environment.update({
        "OPENALADDIN_TRACE_DIR": str(trace_dir),
        "OPENALADDIN_TRACE_FRAMES": str(int(scenario["frames"])),
        "OPENALADDIN_INPUT": str(scenario["input"]),
        "OPENALADDIN_CAPTURE": "full" if bool(scenario.get("capture_vdp", True)) else "ram",
        "OPENALADDIN_CAPTURE_VDP": _bool_env(scenario.get("capture_vdp"), True),
        "OPENALADDIN_TRACE_RNC_LOADS": _bool_env(scenario.get("trace_rnc_loads"), True),
        "OPENALADDIN_TRACE_SCENE_STATES": _bool_env(scenario.get("trace_scene_states"), True),
    })
    for key, env_name in (
        ("load_state", "OPENALADDIN_LOAD_STATE"),
        ("save_frame", "OPENALADDIN_SAVE_FRAME"),
        ("snapshot_frame", "OPENALADDIN_SNAPSHOT_FRAME"),
        ("save_name", "OPENALADDIN_SAVE_NAME"),
        ("snapshot_name", "OPENALADDIN_SNAPSHOT_NAME"),
    ):
        if scenario.get(key) is not None:
            environment[env_name] = str(scenario[key])

    command = [str(ROOT / "tools/openaladdin/mame/run.sh"), str(rom)]
    print(f"[{name}] {scenario['frames']} frames: {scenario['input']}")
    if not dry_run:
        subprocess.run(command, cwd=ROOT, env=environment, check=True)
    debug_log = ROOT / "debug.log"
    saved_log = trace_dir / "debug.log"
    if not dry_run and debug_log.is_file():
        shutil.copyfile(debug_log, saved_log)
        subprocess.run([
            sys.executable,
            str(parser_path),
            "--log",
            str(saved_log),
            "--loader",
            str(loader_path),
            "--output",
            str(trace_dir / "rnc_loads.json"),
        ], cwd=ROOT, check=True)
    return {
        "name": name,
        "description": scenario.get("description", ""),
        "frames": int(scenario["frames"]),
        "input": str(scenario["input"]),
        "trace_dir": _relative(trace_dir),
        "debug_log": _relative(saved_log),
        "load_trace": _relative(trace_dir / "rnc_loads.json"),
        "status": "planned" if dry_run else "completed",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", nargs="?", type=Path, default=_default_rom())
    parser.add_argument(
        "--config",
        type=Path,
        default=ROOT / "re/mame/experiments/capture_matrix.yml",
        help="scenario YAML configuration",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "build/re/rnc-capture-matrix",
        help="matrix output directory",
    )
    parser.add_argument("--scenario", action="append", default=[], help="run only this scenario; repeatable")
    parser.add_argument("--dry-run", action="store_true", help="print scenarios without running MAME")
    args = parser.parse_args()

    rom = args.rom.resolve()
    if not rom.is_file():
        raise SystemExit(f"ROM not found: {rom}")
    config_path = args.config.resolve()
    loader_path = (ROOT / "build/assets/rnc/loader_analysis.json").resolve()
    if not loader_path.is_file():
        raise SystemExit(f"RNC loader analysis not found: {loader_path}; run tools/oa.py assets first")
    output_root = args.output.resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    scenarios = _load_scenarios(config_path, set(args.scenario))
    parser_path = ROOT / "tools/openaladdin/assets/rnc_load_trace.py"
    manifest = {
        "format": "openaladdin-mame-capture-matrix-v1",
        "rom": str(rom),
        "config": _relative(config_path),
        "scenarios": [],
    }
    for scenario in scenarios:
        result = _run_scenario(rom, output_root, scenario, parser_path, loader_path, args.dry_run)
        manifest["scenarios"].append(result)
    matrix_path = output_root / "matrix.json"
    write_json(matrix_path, manifest)
    print(f"matrix manifest: {matrix_path}")
    if args.dry_run:
        return 0

    combined = output_root / "combined"
    subprocess.run([
        sys.executable,
        str(ROOT / "tools/openaladdin/mame/merge_traces.py"),
        str(matrix_path),
        "--output",
        str(combined),
    ], cwd=ROOT, check=True)
    subprocess.run([
        sys.executable,
        str(ROOT / "tools/openaladdin/analysis/scenes.py"),
        "--trace",
        str(combined),
        "--load-trace",
        str(combined / "rnc_loads.json"),
        "--output",
        str(combined / "scene_state_runtime.json"),
    ], cwd=ROOT, check=True)
    subprocess.run([
        sys.executable,
        str(ROOT / "tools/openaladdin/assets/rnc_runtime_cli.py"),
        "--trace",
        str(combined),
        "--load-trace",
        str(combined / "rnc_loads.json"),
    ], cwd=ROOT, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
