#!/usr/bin/env python3
"""Unified OpenAladdin reverse-engineering workflow frontend.

This command intentionally delegates the actual work to the existing tools.
The tracked YAML files and the ROM remain the source of truth; this file only
provides one discoverable entry point and a small amount of orchestration.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Iterable

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parent
sys.path.insert(0, str(SCRIPT_DIR))

from openaladdin.common import hashes, load_yaml, normalize_symbols, parse_int, rom_entries  # noqa: E402


EXPERIMENTS = ROOT / "re/mame/experiments/manifest.yml"
GHIDRA_CONFIG = ROOT / "re/config/ghidra.yml"
ROM_DEFAULT = ROOT / "rom/Disneys_Aladdin_U_p1.bin"


def default_rom() -> Path:
    _, expected, _ = rom_entries()
    configured = ROOT / str(expected.get("expected_filename", "rom/Disneys_Aladdin_U_p1.bin"))
    if configured.is_file():
        return configured
    return ROOT / "rom/aladdin-usa.bin"


def tool_path(name: str) -> Path:
    return ROOT / "tools" / name


def run_tool(name: str, args: Iterable[str] = (), *, env: dict[str, str] | None = None) -> int:
    command = [sys.executable, str(tool_path(name)), *map(str, args)]
    environment = dict(env or os.environ)
    python_path = environment.get("PYTHONPATH")
    environment["PYTHONPATH"] = str(SCRIPT_DIR) + (os.pathsep + python_path if python_path else "")
    return subprocess.run(command, cwd=ROOT, env=environment, check=False).returncode


def run_shell_tool(name: str, args: Iterable[str] = (), *, env: dict[str, str] | None = None) -> int:
    command = [str(tool_path(name)), *map(str, args)]
    return subprocess.run(command, cwd=ROOT, env=env, check=False).returncode


def add_rom_argument(parser: argparse.ArgumentParser, *, positional: bool = False) -> None:
    if positional:
        parser.add_argument("rom", nargs="?", type=Path, default=default_rom())
    else:
        parser.add_argument("--rom", type=Path, default=default_rom())


def resolve(path: Path) -> Path:
    return path if path.is_absolute() else (ROOT / path)


def command_setup(args: argparse.Namespace) -> int:
    return run_tool("openaladdin/ghidra/setup.py")


def command_verify(args: argparse.Namespace) -> int:
    rom = resolve(args.rom)
    forwarded = [str(rom)]
    if args.allow_unverified:
        forwarded.append("--allow-unverified")
    return run_tool("openaladdin/ghidra/verify.py", forwarded)


def command_ghidra_rebuild(args: argparse.Namespace) -> int:
    rom = resolve(args.rom)
    verify_args = [str(rom)]
    if args.allow_unverified:
        verify_args.append("--allow-unverified")
    status = run_tool("openaladdin/ghidra/verify.py", verify_args)
    if status:
        return status

    forwarded = [str(rom)]
    for flag in ("--allow-unverified", "--reuse-project", "--no-analysis"):
        if getattr(args, flag[2:].replace("-", "_")):
            forwarded.append(flag)
    status = run_tool("openaladdin/ghidra/import_rom.py", forwarded)
    if status:
        return status
    errors = validate_knowledge(rom)
    if errors:
        for error in errors:
            print(f"ERROR {error}")
        return 1
    print("validated symbols and types")
    return 0


def load_experiment(name: str) -> dict[str, Any]:
    document = load_yaml(EXPERIMENTS) or {}
    for experiment in document.get("experiments", []):
        if experiment.get("name") == name:
            return dict(experiment)
    known = ", ".join(str(item.get("name")) for item in document.get("experiments", []))
    raise SystemExit(f"unknown trace scenario {name!r}; known scenarios: {known}")


def _symbol_index() -> dict[str, dict[str, Any]]:
    return {str(symbol["name"]): symbol for symbol in normalize_symbols()}


def _condition_action(condition: dict[str, Any] | str, timeout: int, symbols: dict[str, dict[str, Any]]) -> str:
    if isinstance(condition, str):
        match = re.fullmatch(r"\s*([A-Za-z_][A-Za-z0-9_]*)\s*(==|!=|<=|>=|<|>)\s*(-?(?:0[xX][0-9A-Fa-f]+|\d+))\s*", condition)
        if not match:
            raise SystemExit(f"unsupported experiment condition expression: {condition!r}")
        operator = {
            "==": "equals",
            "!=": "not_equals",
            "<": "less_than",
            "<=": "less_equal",
            ">": "greater_than",
            ">=": "greater_equal",
        }[match.group(2)]
        condition = {
            "symbol": match.group(1),
            operator: parse_int(match.group(3)),
        }
    if "pc" in condition:
        target_name = str(condition["pc"])
        target = symbols.get(target_name)
        address = int(target["address"]) if target else parse_int(target_name)
        return f"wait|pc|{address}|u32|eq|{address}|{timeout}"

    symbol_name = condition.get("symbol")
    if symbol_name is not None:
        symbol = symbols.get(str(symbol_name))
        if symbol is None:
            raise SystemExit(f"experiment references unknown symbol {symbol_name!r}")
        address = int(symbol["address"])
        width = str(symbol.get("type", "u8")).lower()
    elif condition.get("memory") is not None:
        address = parse_int(condition["memory"])
        width = str(condition.get("type", "u8")).lower()
    else:
        raise SystemExit(f"experiment condition has no symbol, memory, or pc: {condition}")

    operators = {
        "equals": "eq",
        "equal": "eq",
        "not_equals": "ne",
        "less_than": "lt",
        "less_equal": "le",
        "greater_than": "gt",
        "greater_equal": "ge",
    }
    operation = "eq"
    value: Any = 0
    shorthand = condition.get("condition")
    if shorthand in ("negative", "less_than_zero"):
        return f"wait|memory|{address}|{width}|lt|0|{timeout}"
    if shorthand in ("positive", "greater_than_zero"):
        return f"wait|memory|{address}|{width}|gt|0|{timeout}"
    if shorthand == "zero":
        return f"wait|memory|{address}|{width}|eq|0|{timeout}"
    for key, operator in operators.items():
        if key in condition:
            operation = operator
            value = condition[key]
            break
    else:
        raise SystemExit(f"experiment condition has no comparison: {condition}")
    return f"wait|memory|{address}|{width}|{operation}|{parse_int(value)}|{timeout}"


def experiment_action_protocol(experiment: dict[str, Any]) -> str | None:
    """Compile the YAML experiment subset into a delimiter-safe MAME protocol."""
    if not experiment.get("boot") and not experiment.get("setup") and not experiment.get("actions"):
        return None
    document = load_yaml(EXPERIMENTS) or {}
    symbols = _symbol_index()
    actions: list[str] = []
    boot = experiment.get("boot") or {}
    if boot:
        boot_name = boot.get("scenario")
        boot_scenarios = document.get("boot_scenarios") or {}
        scenario = boot_scenarios.get(boot_name)
        if not scenario or not scenario.get("input"):
            raise SystemExit(f"experiment references unknown boot scenario {boot_name!r}")
        actions.append(f"schedule|{scenario['input']}")

    for group_name in ("setup", "actions"):
        for item in experiment.get(group_name) or []:
            item = item or {}
            if "input" in item:
                input_spec = item["input"] or {}
                requested = input_spec.get("buttons")
                if requested is None:
                    requested = [name for name in ("up", "down", "left", "right", "a", "b", "c", "start") if input_spec.get(name)]
                controls = [str(name) for name in requested]
                token = "+".join(controls) or "none"
                actions.append(f"input|{token}|{int(input_spec.get('frames', 1))}")
                continue
            if "wait" in item or "capture" in item:
                wait_spec = item.get("wait") or item.get("capture") or {}
                if any(key in wait_spec for key in ("symbol", "memory", "pc")):
                    condition = dict(wait_spec)
                    condition.pop("timeout", None)
                else:
                    condition = wait_spec.get("condition") or wait_spec.get("until") or wait_spec
                timeout = int(wait_spec.get("timeout", 120))
                actions.append(_condition_action(condition, timeout, symbols))
                continue
            if "marker" in item:
                actions.append(f"marker|{item['marker']}")
                continue
            raise SystemExit(f"unsupported {group_name} action: {item}")
    return ";".join(actions)


def command_trace(args: argparse.Namespace) -> int:
    experiment = load_experiment(args.scenario)
    rom = resolve(args.rom)
    if not rom.is_file():
        raise SystemExit(f"ROM not found: {rom}")

    trace_dir = resolve(args.trace_dir) if args.trace_dir else ROOT / "build/re/traces" / args.scenario
    environment = os.environ.copy()
    for key in (
        "OPENALADDIN_TRACE_DIR",
        "OPENALADDIN_TRACE_FRAMES",
        "OPENALADDIN_INPUT",
        "OPENALADDIN_STATE_OUTPUT",
        "OPENALADDIN_CAPTURE",
        "OPENALADDIN_TRACE_ACTORS",
        "OPENALADDIN_LOAD_STATE",
        "OPENALADDIN_CAPTURE_VDP",
        "OPENALADDIN_EXPERIMENT_ACTIONS",
    ):
        environment.pop(key, None)
    environment.update({
        "OPENALADDIN_TRACE_DIR": str(trace_dir),
        "OPENALADDIN_TRACE_FRAMES": str(args.frames or experiment["frames"]),
        "OPENALADDIN_INPUT": str(args.input or experiment.get("input", "none")),
        "OPENALADDIN_ROM_SHA256": hashes(rom)["sha256"],
    })
    protocol = experiment_action_protocol(experiment)
    if protocol:
        environment["OPENALADDIN_EXPERIMENT_ACTIONS"] = protocol
    capture = args.capture
    if args.capture_vdp is True and capture == "state":
        capture = "full"
    elif args.capture_vdp is False and capture in ("vdp", "full"):
        capture = "state"
    environment["OPENALADDIN_CAPTURE"] = capture
    if args.state_output:
        environment["OPENALADDIN_STATE_OUTPUT"] = "1"
    if args.actors:
        environment["OPENALADDIN_TRACE_ACTORS"] = "1"
    if args.load_state:
        environment["OPENALADDIN_LOAD_STATE"] = args.load_state
    if args.capture_vdp is not None:
        environment["OPENALADDIN_CAPTURE_VDP"] = "1" if args.capture_vdp else "0"

    status = run_shell_tool("openaladdin/mame/run.sh", [str(rom)], env=environment)
    if status == 0:
        print(f"trace: {trace_dir}")
        if args.state_output:
            print(f"state: {trace_dir / 'state.jsonl'}")
    return status


def load_state_trace(path: Path) -> tuple[dict[str, Any], dict[int, dict[str, Any]], list[dict[str, Any]]]:
    header: dict[str, Any] | None = None
    states: dict[int, dict[str, Any]] = {}
    markers: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise SystemExit(f"{path}:{line_number}: invalid JSON: {error}") from error
        if record.get("type") == "header":
            header = record
        elif record.get("type") == "marker":
            markers.append(record)
        elif record.get("type") in (None, "state", "frame_state"):
            if "frame" in record:
                states[int(record["frame"])] = record
    if header is None:
        raise SystemExit(f"{path}: state trace has no header")
    if not states:
        raise SystemExit(f"{path}: state trace has no state records")
    return header, states, markers


def aligned_trace(
    source: Path,
    destination: Path,
    marker_name: str,
    fields: list[str],
) -> tuple[int, int, dict[str, Any]]:
    header, states, markers = load_state_trace(source)
    matching = [marker for marker in markers if marker.get("name") == marker_name]
    if not matching:
        known = ", ".join(str(marker.get("name")) for marker in markers) or "none"
        raise SystemExit(f"{source}: checkpoint marker {marker_name!r} not found (markers: {known})")
    checkpoint_frame = int(matching[0]["frame"])
    if checkpoint_frame not in states:
        raise SystemExit(f"{source}: checkpoint marker frame {checkpoint_frame} has no state record")

    selected = sorted(frame for frame in states if frame >= checkpoint_frame)
    if not selected or selected[0] != checkpoint_frame:
        raise SystemExit(f"{source}: no state records at or after checkpoint frame {checkpoint_frame}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    aligned_header = dict(header)
    aligned_header["frame_limit"] = len(selected) - 1
    aligned_header["alignment"] = {
        "marker": marker_name,
        "source_frame": checkpoint_frame,
        "fields": fields,
    }
    with destination.open("w", encoding="utf-8") as output:
        output.write(json.dumps(aligned_header, separators=(",", ":")) + "\n")
        for relative_frame, source_frame in enumerate(selected):
            record = json.loads(json.dumps(states[source_frame]))
            record["type"] = "state"
            record["frame"] = relative_frame
            player = record.setdefault("player", {})
            if "grounded" not in player:
                player["grounded"] = int(player.get("vy", 0)) == 0
            output.write(json.dumps(record, separators=(",", ":")) + "\n")
    return len(selected) - 1, checkpoint_frame, states[checkpoint_frame]


def compress_input_schedule(tokens: list[str]) -> str:
    if not tokens:
        return "none"
    result: list[str] = []
    start = 0
    while start < len(tokens):
        end = start + 1
        while end < len(tokens) and tokens[end] == tokens[start]:
            end += 1
        result.append(f"{tokens[start]}*{end - start}")
        start = end
    return ",".join(result)


def command_regression(args: argparse.Namespace) -> int:
    experiment = load_experiment(args.scenario)
    regression = experiment.get("regression") or {}
    marker_name = str(regression.get("checkpoint", "gameplay_checkpoint"))
    fields = list(args.fields or regression.get("fields") or [
        "player.x",
        "player.y",
        "player.vx",
        "player.vy",
        "player.grounded",
    ])
    rom = resolve(args.rom)
    if not rom.is_file():
        raise SystemExit(f"ROM not found: {rom}")

    root_trace = resolve(args.trace_dir) if args.trace_dir else ROOT / "build/re/regression" / args.scenario
    mame_trace = root_trace / "mame"
    aligned = root_trace / "mame-aligned.jsonl"
    native_trace = root_trace / "native.jsonl"
    frame_limit = int(args.frames or experiment["frames"])

    environment = os.environ.copy()
    for key in (
        "OPENALADDIN_TRACE_DIR",
        "OPENALADDIN_TRACE_FRAMES",
        "OPENALADDIN_INPUT",
        "OPENALADDIN_STATE_OUTPUT",
        "OPENALADDIN_CAPTURE",
        "OPENALADDIN_TRACE_ACTORS",
        "OPENALADDIN_LOAD_STATE",
        "OPENALADDIN_CAPTURE_VDP",
        "OPENALADDIN_EXPERIMENT_ACTIONS",
    ):
        environment.pop(key, None)
    environment.update({
        "OPENALADDIN_TRACE_DIR": str(mame_trace),
        "OPENALADDIN_TRACE_FRAMES": str(frame_limit),
        "OPENALADDIN_CAPTURE": "state",
        "OPENALADDIN_ROM_SHA256": hashes(rom)["sha256"],
    })
    protocol = experiment_action_protocol(experiment)
    if protocol:
        environment["OPENALADDIN_EXPERIMENT_ACTIONS"] = protocol

    print(f"regression: running MAME experiment {args.scenario}")
    status = run_shell_tool("openaladdin/mame/run.sh", [str(rom)], env=environment)
    if status:
        return status

    mame_source = mame_trace / "state.jsonl"
    compare_frames, checkpoint_frame, checkpoint = aligned_trace(mame_source, aligned, marker_name, fields)
    _, mame_states, _ = load_state_trace(mame_source)
    input_tokens = [
        str(mame_states[checkpoint_frame + relative].get("input", "none"))
        for relative in range(compare_frames)
    ]

    player = checkpoint.get("player") or {}
    checkpoint_spec = ",".join(str(int(player.get(name, 0))) for name in ("x", "y", "vx", "vy"))
    checkpoint_spec += "," + ("1" if player.get("grounded", int(player.get("vy", 0)) == 0) else "0")
    native_environment = os.environ.copy()
    native_environment["SDL_VIDEODRIVER"] = "dummy"
    native_command = [
        str(ROOT / "run.sh"),
        "--no-window",
        "--frames", str(compare_frames),
        "--state-output", str(native_trace),
        "--input-schedule", compress_input_schedule(input_tokens),
        "--checkpoint-player", checkpoint_spec,
    ]
    print(f"regression: checkpoint {marker_name} at MAME frame {checkpoint_frame}")
    print(f"regression: replaying {compare_frames} post-checkpoint frame(s) natively")
    status = subprocess.run(native_command, cwd=ROOT, env=native_environment, check=False).returncode
    if status:
        return status

    print(f"regression: comparing fields {', '.join(fields)}")
    compare_args: list[str] = [str(aligned), str(native_trace)]
    for field in fields:
        compare_args.extend(["--field", field])
    status = run_tool("openaladdin/mame/compare_state.py", compare_args)
    print(f"regression: MAME trace {mame_trace}")
    print(f"regression: aligned trace {aligned}")
    print(f"regression: native trace {native_trace}")
    return status


def command_decode(args: argparse.Namespace, kind: str) -> int:
    rom = resolve(args.rom)
    if kind == "animation":
        output = resolve(args.output or ROOT / "build/re/animation_streams.json")
        forwarded: list[str] = [str(rom), "--output", str(output)]
        for flag in ("--discover-streams", "--follow-control-flow"):
            if getattr(args, flag[2:].replace("-", "_")):
                forwarded.append(flag)
        if args.max_instructions is not None:
            forwarded.extend(["--max-instructions", str(args.max_instructions)])
        if args.max_bytes is not None:
            forwarded.extend(["--max-bytes", str(args.max_bytes)])
    else:
        output = resolve(args.output or ROOT / "build/re/movement_streams.json")
        forwarded = [str(rom), "--output", str(output)]
        if args.no_follow_control_flow:
            forwarded.append("--no-follow-control-flow")
        if args.max_steps is not None:
            forwarded.extend(["--max-steps", str(args.max_steps)])
        if args.max_bytes is not None:
            forwarded.extend(["--max-bytes", str(args.max_bytes)])

    status = run_tool(f"openaladdin/vm/{kind}.py", forwarded)
    if status or not args.verify:
        return status
    return run_tool("openaladdin/vm/verify.py", [kind, str(rom), str(output)])


def command_assets(args: argparse.Namespace) -> int:
    rom = resolve(args.rom)
    forwarded: list[str] = [str(rom)]
    if args.output:
        forwarded.extend(["--output", str(resolve(args.output))])
    if args.runtime_trace:
        forwarded.extend(["--runtime-trace", str(resolve(args.runtime_trace))])
    if args.runtime_load_trace:
        forwarded.extend(["--runtime-load-trace", str(resolve(args.runtime_load_trace))])
    for flag in ("--no-levels", "--no-sprites", "--no-animations"):
        if getattr(args, flag[2:].replace("-", "_")):
            forwarded.append(flag)
    return run_tool("openaladdin/assets/extract.py", forwarded)


def _field_size(type_name: str) -> int:
    normalized = type_name.strip().lower()
    if normalized in {"u8", "i8", "s8", "byte", "bool"}:
        return 1
    if normalized in {"u16", "i16", "s16", "word"}:
        return 2
    if normalized in {"u32", "i32", "s32", "long", "rom_pointer", "pointer"}:
        return 4
    if normalized.startswith("u8[") and normalized.endswith("]"):
        return int(normalized[3:-1], 0)
    if normalized.startswith("u16[") and normalized.endswith("]"):
        return 2 * int(normalized[4:-1], 0)
    if normalized.startswith("u32[") and normalized.endswith("]"):
        return 4 * int(normalized[4:-1], 0)
    raise ValueError(f"unknown field type {type_name!r}")


def validate_knowledge(rom: Path) -> list[str]:
    errors: list[str] = []
    try:
        symbols = normalize_symbols()
    except (OSError, ValueError) as error:
        return [f"symbols: {error}"]

    names: dict[str, dict[str, Any]] = {}
    for symbol in symbols:
        name = str(symbol["name"])
        if name in names:
            errors.append(f"duplicate symbol name: {name}")
        names[name] = symbol
        address = int(symbol["address"])
        category = symbol["category"]
        if category == "ram" and not 0xFF0000 <= address <= 0xFFFFFF:
            errors.append(f"RAM symbol outside work RAM: {name} at 0x{address:06X}")
        if category == "functions" and not 0 <= address < rom.stat().st_size:
            errors.append(f"function outside ROM: {name} at 0x{address:06X}")
        if category == "data" and address >= 0x1000000:
            errors.append(f"data symbol outside 24-bit address space: {name}")

    for type_path in sorted((ROOT / "re/types").glob("*.yml")):
        try:
            definition = load_yaml(type_path) or {}
            size = parse_int(definition.get("size", 0))
            occupied: dict[int, str] = {}
            for raw_offset, field in (definition.get("fields") or {}).items():
                offset = parse_int(raw_offset)
                width = _field_size(str((field or {}).get("type", "")))
                if offset < 0 or offset + width > size:
                    errors.append(f"{type_path.relative_to(ROOT)} field at 0x{offset:X} exceeds size 0x{size:X}")
                for byte in range(offset, offset + width):
                    previous = occupied.get(byte)
                    if previous:
                        errors.append(f"{type_path.relative_to(ROOT)} fields overlap at byte 0x{byte:X}: {previous}")
                    occupied[byte] = str((field or {}).get("name", raw_offset))
        except (OSError, TypeError, ValueError) as error:
            errors.append(f"{type_path.relative_to(ROOT)}: {error}")
    return errors


def asset_current(rom: Path) -> bool:
    manifest = ROOT / "build/assets/manifest.json"
    if not manifest.is_file():
        return False
    try:
        identity = json.loads(manifest.read_text(encoding="utf-8")).get("rom", {})
        actual = hashes(rom)
        return all(identity.get(key) == actual[key] for key in ("size", "crc32", "sha1", "sha256"))
    except (OSError, ValueError, KeyError):
        return False


def project_current(rom: Path) -> bool:
    try:
        config = load_yaml(GHIDRA_CONFIG) or {}
        project_dir = ROOT / config["ghidra"]["project_dir"]
        project_name = config["ghidra"]["project_name"]
        analysis_path = ROOT / "build/re/analysis.json"
        if not (analysis_path.is_file() and (project_dir / f"{project_name}.gpr").is_file()):
            return False
        analysis = json.loads(analysis_path.read_text(encoding="utf-8"))
        return analysis.get("rom_identity", {}).get("sha256") == hashes(rom)["sha256"]
    except (OSError, KeyError, TypeError, ValueError):
        return False


def generated_opcode_count(path: Path, *, movement: bool) -> tuple[int, int]:
    total = 0x15
    if not path.is_file():
        return 0, total
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
        found: set[str] = set()
        for stream in (document.get("streams") or {}).values():
            if movement:
                for step in stream.get("steps", []):
                    found.update(command.get("opcode") for command in step.get("commands", []) if command.get("opcode"))
            else:
                found.update(instruction.get("opcode") for instruction in stream.get("instructions", []) if instruction.get("kind") == "command")
        return len(found), total
    except (OSError, TypeError, ValueError, json.JSONDecodeError):
        return 0, total


def print_status(rom: Path) -> int:
    default_name, expected, _ = rom_entries()
    actual = hashes(rom) if rom.is_file() else None
    rom_ok = bool(actual and all(str(expected.get(key, "")).upper() == str(actual[key]).upper() for key in ("size", "crc32", "sha1", "sha256")))
    knowledge_errors = validate_knowledge(rom) if rom.is_file() else ["ROM not found"]
    ghidra = load_yaml(GHIDRA_CONFIG) or {}
    install = ROOT / ghidra.get("ghidra", {}).get("install_dir", ".tools/ghidra")
    ghidra_ok = (install / "support" / ("pyghidraRun.bat" if os.name == "nt" else "pyghidraRun")).is_file()
    animation_count, animation_total = generated_opcode_count(ROOT / "build/re/animation_streams.json", movement=False)
    movement_count, movement_total = generated_opcode_count(ROOT / "build/re/movement_streams.json", movement=True)
    try:
        functions = len(load_yaml(ROOT / "re/symbols/functions.yml") or {})
        confirmed_ram = sum(1 for value in (load_yaml(ROOT / "re/symbols/ram.yml") or {}).values() if (value or {}).get("confidence") == "confirmed")
        actor = load_yaml(ROOT / "re/types/actor.yml") or {}
        actor_fields = len(actor.get("fields") or {})
    except (OSError, TypeError, ValueError):
        functions = confirmed_ram = actor_fields = 0

    print(f"ROM               {'OK' if rom_ok else 'MISSING/MISMATCH'}")
    print(f"Ghidra             {'OK' if ghidra_ok else 'missing'}")
    print(f"Ghidra project     {'current' if project_current(rom) else 'stale/missing'}")
    print(f"symbols            {'valid' if not knowledge_errors else f'invalid ({len(knowledge_errors)})'}")
    print(f"types              {'valid' if not knowledge_errors else f'check validate ({len(knowledge_errors)})'}")
    print(f"assets             {'current' if rom.is_file() and asset_current(rom) else 'missing/stale'}")
    mame = ROOT / "external/mame/mame"
    print(f"MAME               {'available' if mame.is_file() and os.access(mame, os.X_OK) else 'missing'}")
    print()
    print(f"Known functions     {functions}")
    print(f"Confirmed RAM       {confirmed_ram}")
    print(f"Actor fields        {actor_fields} named / {parse_int(actor.get('size', 0)) if actor else 0} bytes")
    print(f"Animation opcodes   {animation_count}/{animation_total}")
    print(f"Movement opcodes    {movement_count}/{movement_total}")
    if actual is None:
        print(f"ROM identity        {default_name}: unavailable")
    else:
        print(f"ROM identity        {actual['sha1']}")
    return 0 if rom_ok and not knowledge_errors else 1


def command_validate(args: argparse.Namespace) -> int:
    rom = resolve(args.rom)
    status = command_verify(argparse.Namespace(rom=rom, allow_unverified=args.allow_unverified))
    if status:
        return status
    errors = validate_knowledge(rom)
    if errors:
        for error in errors:
            print(f"ERROR {error}")
        return 1
    print("validated symbols, types, and ROM identity")

    if not args.skip_assets:
        status = run_tool("openaladdin/assets/validate.py", ["--assets", str(resolve(args.assets)), "--rom", str(rom)])
        if status:
            return status
    if not args.skip_scene:
        loader = ROOT / "build/assets/rnc/loader_analysis.json"
        if loader.is_file():
            status = run_tool("openaladdin/assets/validate_scene_resources.py")
            if status:
                return status
        else:
            print("scene resources: skipped (asset extraction is not present)")
    for kind, path in (("animation", ROOT / "build/re/animation_streams.json"), ("movement", ROOT / "build/re/movement_streams.json")):
        if path.is_file():
            status = run_tool("openaladdin/vm/verify.py", [kind, str(rom), str(path)])
            if status:
                return status
        else:
            print(f"{kind} streams: skipped (decode output is not present)")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="oa", description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    setup = commands.add_parser("setup", help="install the pinned Ghidra toolchain")
    setup.set_defaults(function=command_setup)

    verify = commands.add_parser("verify", help="verify the configured ROM identity")
    add_rom_argument(verify, positional=True)
    verify.add_argument("--allow-unverified", action="store_true")
    verify.set_defaults(function=command_verify)

    ghidra = commands.add_parser("ghidra", help="manage Ghidra analysis")
    ghidra_commands = ghidra.add_subparsers(dest="ghidra_command", required=True)
    rebuild = ghidra_commands.add_parser("rebuild", help="verify and rebuild the local project")
    add_rom_argument(rebuild)
    rebuild.add_argument("--allow-unverified", action="store_true")
    rebuild.add_argument("--reuse-project", action="store_true")
    rebuild.add_argument("--no-analysis", action="store_true")
    rebuild.set_defaults(function=command_ghidra_rebuild)

    trace = commands.add_parser("trace", help="run a named repeatable MAME experiment")
    trace.add_argument("scenario", help="experiment name from re/mame/experiments/manifest.yml")
    add_rom_argument(trace)
    trace.add_argument("--frames", type=int)
    trace.add_argument("--input")
    trace.add_argument("--trace-dir", type=Path)
    trace.add_argument("--capture", choices=("state", "ram", "vdp", "full"), default="state")
    trace.add_argument("--state-output", action="store_true")
    trace.add_argument("--actors", action="store_true")
    trace.add_argument("--load-state")
    trace.add_argument("--capture-vdp", action=argparse.BooleanOptionalAction, default=None)
    trace.set_defaults(function=command_trace)

    regression = commands.add_parser("regression", help="differentially compare MAME and native gameplay")
    regression.add_argument("scenario")
    add_rom_argument(regression)
    regression.add_argument("--frames", type=int)
    regression.add_argument("--trace-dir", type=Path)
    regression.add_argument("--field", dest="fields", action="append")
    regression.set_defaults(function=command_regression)

    decode = commands.add_parser("decode", help="decode a VM stream family")
    decode_commands = decode.add_subparsers(dest="decode_kind", required=True)
    animation = decode_commands.add_parser("animation")
    add_rom_argument(animation)
    animation.add_argument("--output", type=Path)
    animation.add_argument("--discover-streams", action="store_true")
    animation.add_argument("--follow-control-flow", action="store_true")
    animation.add_argument("--max-instructions", type=int)
    animation.add_argument("--max-bytes", type=int)
    animation.add_argument("--verify", action="store_true")
    animation.set_defaults(function=lambda args: command_decode(args, "animation"))
    movement = decode_commands.add_parser("movement")
    add_rom_argument(movement)
    movement.add_argument("--output", type=Path)
    movement.add_argument("--no-follow-control-flow", action="store_true")
    movement.add_argument("--max-steps", type=int)
    movement.add_argument("--max-bytes", type=lambda value: int(value, 0))
    movement.add_argument("--verify", action="store_true")
    movement.set_defaults(function=lambda args: command_decode(args, "movement"))

    assets = commands.add_parser("assets", help="extract native assets")
    add_rom_argument(assets)
    assets.add_argument("--output", type=Path)
    assets.add_argument("--runtime-trace", type=Path)
    assets.add_argument("--runtime-load-trace", type=Path)
    assets.add_argument("--no-levels", action="store_true")
    assets.add_argument("--no-sprites", action="store_true")
    assets.add_argument("--no-animations", action="store_true")
    assets.set_defaults(function=command_assets)

    validate = commands.add_parser("validate", help="validate tracked knowledge and generated reports")
    add_rom_argument(validate)
    validate.add_argument("--assets", type=Path, default=ROOT / "build/assets")
    validate.add_argument("--allow-unverified", action="store_true")
    validate.add_argument("--skip-assets", action="store_true")
    validate.add_argument("--skip-scene", action="store_true")
    validate.set_defaults(function=command_validate)

    compare = commands.add_parser("compare", help="find the first divergent frame in two state traces")
    compare.add_argument("genesis", type=Path)
    compare.add_argument("openaladdin", type=Path)
    compare.set_defaults(function=lambda args: run_tool("openaladdin/mame/compare_state.py", [str(resolve(args.genesis)), str(resolve(args.openaladdin))]))

    status = commands.add_parser("status", help="show repository and RE progress status")
    add_rom_argument(status)
    status.set_defaults(function=lambda args: print_status(resolve(args.rom)))
    return parser


def main() -> int:
    args = build_parser().parse_args()
    return int(args.function(args))


if __name__ == "__main__":
    raise SystemExit(main())
