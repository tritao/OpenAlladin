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
import shutil
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


def experiment_memory_pokes(experiment: dict[str, Any]) -> tuple[int, str] | None:
    """Compile a named experiment's deterministic RAM fixture into Lua pokes."""
    fixture = experiment.get("pokes") or {}
    if not fixture:
        return None
    if not isinstance(fixture, dict):
        raise SystemExit("experiment pokes must be a mapping with frame and memory")
    if "frame" not in fixture or "memory" not in fixture:
        raise SystemExit("experiment pokes require frame and memory")

    symbols = _symbol_index()
    entries: list[str] = []
    for item in fixture["memory"] or []:
        if not isinstance(item, dict):
            raise SystemExit(f"experiment memory poke must be a mapping: {item!r}")
        if "symbol" in item:
            symbol_name = str(item["symbol"])
            symbol = symbols.get(symbol_name)
            if symbol is None:
                raise SystemExit(f"experiment poke references unknown symbol {symbol_name!r}")
            address = int(symbol["address"])
        elif "address" in item:
            address = parse_int(item["address"])
        else:
            raise SystemExit(f"experiment poke has no symbol or address: {item!r}")
        address += parse_int(item.get("offset", 0))
        value = parse_int(item.get("value", 0))
        width = str(item.get("width", "u8")).lower()
        if width not in ("u8", "u16", "u32"):
            raise SystemExit(f"experiment poke has unsupported width {width!r}")
        entries.append(f"0x{address:06X}={value}:{width}")
    if not entries:
        raise SystemExit("experiment pokes memory list is empty")
    return parse_int(fixture["frame"]), ",".join(entries)


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
        "OPENALADDIN_STATE_SYNC",
        "OPENALADDIN_TRACE_EDGES",
        "OPENALADDIN_TRACE_AUDIO",
        "OPENALADDIN_TRACE_AUDIO_MAILBOX",
        "OPENALADDIN_TRACE_AUDIO_MAILBOX_READS",
        "OPENALADDIN_AUDIO_MAILBOX_READ_FRAMES",
        "OPENALADDIN_TRACE_AUDIO_COMMANDS",
        "OPENALADDIN_POKE_FRAME",
        "OPENALADDIN_POKE_MEMORY",
        "OPENALADDIN_CHECKPOINTS",
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
    memory_pokes = experiment_memory_pokes(experiment)
    if memory_pokes:
        environment["OPENALADDIN_POKE_FRAME"] = str(memory_pokes[0])
        environment["OPENALADDIN_POKE_MEMORY"] = memory_pokes[1]
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
    if args.checkpoints:
        environment["OPENALADDIN_CHECKPOINTS"] = args.checkpoints
    if args.capture_vdp is not None:
        environment["OPENALADDIN_CAPTURE_VDP"] = "1" if args.capture_vdp else "0"
    if args.state_sync:
        environment["OPENALADDIN_STATE_SYNC"] = "1"
        environment["OPENALADDIN_STATE_OUTPUT"] = "1"
    if args.edges:
        environment["OPENALADDIN_TRACE_EDGES"] = "1"
    if args.audio or args.audio_mailbox or args.audio_mailbox_reads or args.audio_read_frame:
        environment["OPENALADDIN_TRACE_AUDIO"] = "1"
    if args.audio_mailbox or args.audio_mailbox_reads or args.audio_read_frame:
        environment["OPENALADDIN_TRACE_AUDIO_MAILBOX"] = "1"
    if args.audio_mailbox_reads or args.audio_read_frame:
        environment["OPENALADDIN_TRACE_AUDIO_MAILBOX_READS"] = "1"
    if args.audio_read_frame:
        environment["OPENALADDIN_AUDIO_MAILBOX_READ_FRAMES"] = ",".join(args.audio_read_frame)
    if args.audio_commands:
        environment["OPENALADDIN_TRACE_AUDIO_COMMANDS"] = "1"

    status = run_shell_tool("openaladdin/mame/run.sh", [str(rom)], env=environment)
    if status == 0:
        if args.audio_commands:
            debug_log = ROOT / "debug.log"
            if debug_log.is_file():
                trace_dir.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(debug_log, trace_dir / "debug.log")
        if args.state_sync:
            synchronize_state_trace(trace_dir)
        print(f"trace: {trace_dir}")
        if args.state_output:
            print(f"state: {trace_dir / 'state.jsonl'}")
    return status


def command_audio_driver(args: argparse.Namespace) -> int:
    return run_tool(
        "openaladdin/mame/z80_sound.py",
        [str(resolve(args.rom)), "--output", str(resolve(args.output))],
    )


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


SYNC_PATTERN = re.compile(
    r"OPENALADDIN_SYNC frame=(?P<frame>\d+) pc=(?P<pc>[0-9A-F]+) "
    r"x=(?P<x>[0-9A-F]+) y=(?P<y>[0-9A-F]+) "
    r"wx=(?P<world_x>[0-9A-F]+) wy=(?P<world_y>[0-9A-F]+) "
    r"vx=(?P<vx>[0-9A-F]+) vy=(?P<vy>[0-9A-F]+) "
    r"grounded=(?P<grounded>[0-9A-F]+) "
    r"frameptr=(?P<frame_ptr>[0-9A-F]+) animpc=(?P<animation_pc>[0-9A-F]+) "
    r"animtimer=(?P<animation_timer>[0-9A-F]+) "
    r"camx=(?P<camera_x>[0-9A-F]+) camy=(?P<camera_y>[0-9A-F]+) "
    r"refx=(?P<reference_x>[0-9A-F]+) refy=(?P<reference_y>[0-9A-F]+) "
    r"sx=(?P<scroll_x>[0-9A-F]+) sy=(?P<scroll_y>[0-9A-F]+) "
    r"thx=(?P<horizontal_threshold>[0-9A-F]+) "
    r"thy=(?P<vertical_threshold>[0-9A-F]+) "
    r"delay=(?P<update_delay>[0-9A-F]+) special=(?P<special_mode>[0-9A-F]+)"
)


def _signed_u16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def _sync_record(match: re.Match[str]) -> dict[str, int]:
    values = {
        name: int(value, 10 if name == "frame" else 16)
        for name, value in match.groupdict().items()
    }
    values["vx"] = _signed_u16(values["vx"])
    values["vy"] = _signed_u16(values["vy"])
    values["scroll_x"] = _signed_u16(values["scroll_x"])
    values["scroll_y"] = _signed_u16(values["scroll_y"])
    return values


def synchronize_state_trace(trace_dir: Path) -> int:
    """Replace video-boundary state samples with stable game-loop samples.

    MAME's frame_done callback is tied to the video device, not the game's
    update loop. It can therefore observe player/camera RAM between two
    instructions. The debugger breakpoint is placed at the start of the
    game's per-frame update and reports the completed state for the following
    trace frame. The +1 mapping below is intentional and is part of the
    openaladdin-frame-state-v1 capture contract.
    """

    state_path = trace_dir / "state.jsonl"
    debug_path = trace_dir / "debug.log"
    frame_trace_path = trace_dir / "trace_boot.jsonl"
    if not state_path.is_file():
        raise SystemExit(f"{state_path}: synchronized capture has no state trace")
    if not debug_path.is_file():
        raise SystemExit(f"{debug_path}: synchronized capture has no MAME debugger log")

    records: list[dict[str, Any]] = []
    header: dict[str, Any] | None = None
    states: dict[int, dict[str, Any]] = {}
    for line_number, line in enumerate(state_path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise SystemExit(f"{state_path}:{line_number}: invalid JSON: {error}") from error
        records.append(record)
        if record.get("type") == "header":
            header = record
        elif record.get("type") in (None, "state", "frame_state") and "frame" in record:
            states[int(record["frame"])] = record
    if header is None:
        raise SystemExit(f"{state_path}: synchronized capture has no state header")

    frame_metadata: dict[int, dict[str, Any]] = {}
    frame_markers: list[dict[str, Any]] = []
    if frame_trace_path.is_file():
        for line_number, line in enumerate(frame_trace_path.read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                raise SystemExit(f"{frame_trace_path}:{line_number}: invalid JSON: {error}") from error
            if record.get("type") == "frame" and "frame" in record:
                frame_metadata[int(record["frame"])] = record
            elif record.get("type") == "marker":
                frame_markers.append(record)

    synchronized: dict[int, dict[str, int]] = {}
    for line in debug_path.read_text(encoding="utf-8").splitlines():
        match = SYNC_PATTERN.search(line)
        if match:
            parsed = _sync_record(match)
            synchronized[parsed["frame"] + 1] = parsed
    if not synchronized:
        raise SystemExit(f"{debug_path}: no OPENALADDIN_SYNC records found")

    all_frames = set(states)
    all_frames.update(synchronized)
    for frame in sorted(synchronized):
        sync = synchronized[frame]
        previous = max((candidate for candidate in states if candidate < frame), default=None)
        base = states.get(frame) or states.get(previous or 0)
        if base is None:
            raise SystemExit(f"{state_path}: cannot construct synchronized frame {frame}")
        record = json.loads(json.dumps(base))
        record["type"] = "state"
        record["frame"] = frame

        metadata = frame_metadata.get(frame)
        if metadata is not None:
            if "input" in metadata:
                record["input"] = metadata["input"]
            if isinstance(metadata.get("scene"), dict):
                record["scene"] = metadata["scene"]
            if isinstance(metadata.get("terrain"), dict):
                record["terrain"] = metadata["terrain"]

        player = record.setdefault("player", {})
        for name in ("x", "y", "world_x", "world_y", "vx", "vy", "frame_ptr", "animation_pc", "animation_timer"):
            player[name] = sync[name]
        # Lua's canonical state schema treats TERRAIN_LANDING_STATE == 1 as
        # grounded.  0xFF is the active response latch during the jump
        # transition, not the externally reported grounded boolean.
        player["grounded"] = sync["grounded"] == 1

        camera = record.setdefault("camera", {})
        for name in (
            "camera_x",
            "camera_y",
            "reference_x",
            "reference_y",
            "scroll_x",
            "scroll_y",
            "horizontal_threshold",
            "vertical_threshold",
            "update_delay",
            "special_mode",
        ):
            camera[name.removeprefix("camera_")] = sync[name]
        camera["state_08"] = sync["special_mode"] != 0
        states[frame] = record

    # Keep the header and marker records, but emit the completed state stream
    # in frame order so downstream tools do not need to know how the debugger
    # records were merged.
    markers = [record for record in records if record.get("type") == "marker"]
    known_markers = {(record.get("frame"), record.get("name")) for record in markers}
    markers.extend(
        record for record in frame_markers
        if (record.get("frame"), record.get("name")) not in known_markers
    )
    with state_path.open("w", encoding="utf-8") as output:
        output.write(json.dumps(header, separators=(",", ":")) + "\n")
        for frame in sorted(all_frames | set(states)):
            if frame in states:
                output.write(json.dumps(states[frame], separators=(",", ":")) + "\n")
            for marker in markers:
                if int(marker.get("frame", -1)) == frame:
                    output.write(json.dumps(marker, separators=(",", ":")) + "\n")
    return len(synchronized)


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
        "OPENALADDIN_STATE_SYNC",
        "OPENALADDIN_TRACE_EDGES",
    ):
        environment.pop(key, None)
    state_sync = os.environ.get("OPENALADDIN_STATE_SYNC", "1") == "1"
    environment.update({
        "OPENALADDIN_TRACE_DIR": str(mame_trace),
        # The synchronized breakpoint reports the state for the following
        # frame, so capture one extra frame to cover the requested endpoint.
        "OPENALADDIN_TRACE_FRAMES": str(frame_limit + 1 if state_sync else frame_limit),
        "OPENALADDIN_CAPTURE": "state",
        "OPENALADDIN_ROM_SHA256": hashes(rom)["sha256"],
    })
    if any(field.startswith("actors[") for field in fields):
        environment["OPENALADDIN_TRACE_ACTORS"] = "1"
    if state_sync:
        environment["OPENALADDIN_STATE_SYNC"] = "1"
    protocol = experiment_action_protocol(experiment)
    if protocol:
        environment["OPENALADDIN_EXPERIMENT_ACTIONS"] = protocol

    print(f"regression: running MAME experiment {args.scenario}")
    status = run_shell_tool("openaladdin/mame/run.sh", [str(rom)], env=environment)
    if status:
        return status
    if state_sync:
        synchronize_state_trace(mame_trace)

    mame_source = mame_trace / "state.jsonl"
    compare_frames, checkpoint_frame, checkpoint = aligned_trace(mame_source, aligned, marker_name, fields)
    _, mame_states, _ = load_state_trace(mame_source)
    # The native state file contains an initial checkpoint record at frame 0
    # before its first update.  MAME's frame record includes the input that
    # produced that state, so the first native update must consume MAME's next
    # frame token.  Keeping this one-frame offset is what makes the aligned
    # state 0 a true pre-input checkpoint rather than a duplicated update.
    input_tokens = [
        str(mame_states[checkpoint_frame + relative + 1].get("input", "none"))
        for relative in range(compare_frames)
    ]

    player = checkpoint.get("player") or {}
    checkpoint_spec = ",".join(str(int(player.get(name, 0))) for name in ("x", "y", "vx", "vy"))
    checkpoint_spec += "," + ("1" if player.get("grounded", int(player.get("vy", 0)) == 0) else "0")
    camera = checkpoint.get("camera") or {}
    camera_spec = ",".join(
        str(int(camera.get(name, 0)))
        for name in ("x", "y", "reference_x", "reference_y", "scroll_x", "scroll_y")
    )
    camera_spec += "," + str(int((checkpoint.get("scene") or {}).get("state", 1)))
    if all(name in camera for name in ("horizontal_threshold", "vertical_threshold", "update_delay")):
        camera_spec += "," + ",".join(
            str(int(camera[name]))
            for name in ("horizontal_threshold", "vertical_threshold", "update_delay")
        )
    native_environment = os.environ.copy()
    native_environment["SDL_VIDEODRIVER"] = "dummy"
    native_command = [
        str(ROOT / "run.sh"),
        "--no-window",
        "--frames", str(compare_frames),
        "--state-output", str(native_trace),
        "--input-schedule", compress_input_schedule(input_tokens),
        "--checkpoint-player", checkpoint_spec,
        "--checkpoint-camera", camera_spec,
    ]
    if player.get("frame_ptr"):
        native_command.extend(["--checkpoint-frame-ptr", str(int(player["frame_ptr"]))])
    if player.get("animation_pc"):
        native_command.extend([
            "--checkpoint-animation",
            f"{int(player['animation_pc'])},{int(player.get('animation_timer', 0))}",
        ])
    actor_records = regression.get("actor_records")
    if actor_records:
        native_command.extend(["--actor-records", str(resolve(Path(actor_records)))])
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


def command_coverage_merge(args: argparse.Namespace) -> int:
    forwarded: list[str] = []
    if args.trace_dirs:
        forwarded.extend(str(resolve(path)) for path in args.trace_dirs)
    else:
        forwarded.extend(["--trace-root", str(resolve(args.trace_root))])
    forwarded.extend(["--output", str(resolve(args.output))])
    return run_tool("openaladdin/mame/coverage.py", forwarded)


def command_coverage_import_ghidra(args: argparse.Namespace) -> int:
    forwarded = [str(resolve(args.coverage)), "--output", str(resolve(args.output))]
    if args.project_dir:
        forwarded.extend(["--project-dir", str(resolve(args.project_dir))])
    return run_tool("openaladdin/ghidra/import_coverage.py", forwarded)


def command_coverage_gaps(args: argparse.Namespace) -> int:
    forwarded = [
        str(resolve(args.coverage)),
        "--rom", str(resolve(args.rom)),
        "--output", str(resolve(args.output)),
    ]
    return run_tool("openaladdin/mame/coverage_gaps.py", forwarded)


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

    for actor_path in sorted((ROOT / "re/actors").glob("*.tsv")):
        current_frame: int | None = None
        seen_slots: set[tuple[int | None, int]] = set()
        records = 0
        try:
            for line_number, line in enumerate(actor_path.read_text(encoding="utf-8").splitlines(), 1):
                stripped = line.strip()
                if not stripped or stripped.startswith("#"):
                    continue
                fields = stripped.split()
                if fields[0] == "@frame":
                    if len(fields) != 2:
                        errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: invalid frame marker")
                        continue
                    current_frame = int(fields[1], 0)
                    continue
                if len(fields) < 8 or len(fields) > 14:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: expected 8..14 actor fields")
                    continue
                if "timeline" in actor_path.name and current_frame is None:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: record precedes frame marker")
                values = [int(value, 0) for value in fields]
                slot, actor_type, x, y, movement_pc, frame_ptr, animation_pc, flags = values[:8]
                if not 0 <= slot < 32:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: slot outside 0..31")
                for name, value, maximum in (
                    ("type", actor_type, 0xFF),
                    ("x", x, 0xFFFF),
                    ("y", y, 0xFFFF),
                    ("movement_pc", movement_pc, 0xFFFFFF),
                    ("frame_ptr", frame_ptr, 0xFFFFFFFF),
                    ("animation_pc", animation_pc, 0xFFFFFFFF),
                    ("flags", flags, 0xFF),
                ):
                    if not 0 <= value <= maximum:
                        errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: {name} outside range")
                if len(values) >= 9 and not 0 <= values[8] <= 0xFF:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: facing_x_flip outside range")
                if len(values) >= 10 and not 0 <= values[9] <= 0xFF:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: facing_y_flip outside range")
                if len(values) >= 11 and not 0 <= values[10] <= 0xFF:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: movement_command_timer outside range")
                if len(values) >= 12 and not 0 <= values[11] <= 0xFFFFFFFF:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: movement_loop_pc outside range")
                if len(values) >= 13 and not 0 <= values[12] <= 0xFF:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: movement_loop_timer outside range")
                if len(values) >= 14 and not 0 <= values[13] <= 0xFFFFFFFF:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: movement_return_pc outside range")
                key = (current_frame, slot)
                if key in seen_slots:
                    errors.append(f"{actor_path.relative_to(ROOT)}:{line_number}: duplicate slot {slot}")
                seen_slots.add(key)
                records += 1
            if records == 0:
                errors.append(f"{actor_path.relative_to(ROOT)}: no actor records")
        except (OSError, TypeError, ValueError) as error:
            errors.append(f"{actor_path.relative_to(ROOT)}: {error}")
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
    coverage_path = ROOT / "build/re/coverage.json"
    try:
        coverage = json.loads(coverage_path.read_text(encoding="utf-8"))
        summary = coverage.get("summary") or {}
        print(
            f"Runtime coverage    {summary.get('unique_pc_count', 0)} PCs / "
            f"{summary.get('scenario_count', 0)} scenarios / "
            f"{summary.get('unique_edge_count', 0)} edges"
        )
    except (OSError, TypeError, ValueError, json.JSONDecodeError):
        print("Runtime coverage    missing")
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
    actor_records = sorted((ROOT / "re/actors").glob("*.tsv"))
    print(f"validated actor records: {len(actor_records)} files")

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
    trace.add_argument(
        "--checkpoints",
        help="named MAME save states as frame=name pairs, e.g. 0=boot,1245=level01-entry",
    )
    trace.add_argument("--capture-vdp", action=argparse.BooleanOptionalAction, default=None)
    trace.add_argument("--state-sync", action="store_true", help="sample state at the stable game-loop boundary")
    trace.add_argument("--edges", action="store_true", help="capture indirect dispatch targets in MAME debug.log")
    trace.add_argument("--audio", action="store_true", help="capture YM2612/PSG register writes to sound_writes.jsonl")
    trace.add_argument("--audio-mailbox", action="store_true", help="also capture 68000 writes to the Z80 sound mailbox")
    trace.add_argument("--audio-mailbox-reads", action="store_true", help="trace selected Z80 mailbox-read frames; pair with --audio-read-frame")
    trace.add_argument("--audio-read-frame", action="append", help="hex frame to inspect for Z80 mailbox reads")
    trace.add_argument("--audio-commands", action="store_true", help="trace ROM music/SFX command dispatches in MAME debug.log")
    trace.set_defaults(function=command_trace)

    audio_driver = commands.add_parser(
        "audio-driver",
        help="extract and map the ROM-resident Genesis Z80 sound driver",
    )
    add_rom_argument(audio_driver)
    audio_driver.add_argument(
        "--output",
        type=Path,
        default=Path("build/re/z80-sound-driver"),
        help="output directory for driver.bin and driver.json",
    )
    audio_driver.set_defaults(function=command_audio_driver)

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
    compare.add_argument(
        "--field",
        action="append",
        dest="fields",
        help="compare only this dotted state field; repeat for multiple fields",
    )
    compare.set_defaults(function=lambda args: run_tool(
        "openaladdin/mame/compare_state.py",
        [str(resolve(args.genesis)), str(resolve(args.openaladdin))]
        + sum((["--field", field] for field in (args.fields or [])), []),
    ))

    compare_collision = commands.add_parser(
        "compare-collision",
        help="compare resolved player/actor collision boxes and transition frames",
    )
    compare_collision.add_argument("genesis", type=Path)
    compare_collision.add_argument("openaladdin", type=Path)
    compare_collision.add_argument(
        "--actor-slot",
        action="append",
        type=lambda value: int(value, 0),
        dest="actor_slots",
        help="compare this actor slot; repeat for multiple slots",
    )
    compare_collision.add_argument(
        "--transition-type",
        type=lambda value: int(value, 0),
        help="report/check the first frame where each selected actor reaches this type",
    )
    compare_collision.set_defaults(function=lambda args: run_tool(
        "openaladdin/mame/compare_collision.py",
        [str(resolve(args.genesis)), str(resolve(args.openaladdin))]
        + sum((["--actor-slot", str(slot)] for slot in (args.actor_slots or [])), [])
        + (["--transition-type", str(args.transition_type)] if args.transition_type is not None else []),
    ))

    coverage = commands.add_parser("coverage", help="merge and import dynamic MAME execution observations")
    coverage_commands = coverage.add_subparsers(dest="coverage_command", required=True)
    coverage_merge = coverage_commands.add_parser("merge", help="merge sampled frame PCs from MAME traces")
    coverage_merge.add_argument("trace_dirs", nargs="*", type=Path)
    coverage_merge.add_argument("--trace-root", type=Path, default=ROOT / "build/re/traces")
    coverage_merge.add_argument("--output", type=Path, default=ROOT / "build/re/coverage.json")
    coverage_merge.set_defaults(function=command_coverage_merge)
    coverage_import = coverage_commands.add_parser("import-ghidra", help="bookmark observed PCs in Ghidra")
    coverage_import.add_argument("coverage", nargs="?", type=Path, default=ROOT / "build/re/coverage.json")
    coverage_import.add_argument("--output", type=Path, default=ROOT / "build/re/coverage-ghidra.json")
    coverage_import.add_argument("--project-dir", type=Path)
    coverage_import.set_defaults(function=command_coverage_import_ghidra)
    coverage_gaps = coverage_commands.add_parser("gaps", help="report unobserved indirect-dispatch table entries")
    coverage_gaps.add_argument("coverage", nargs="?", type=Path, default=ROOT / "build/re/coverage.json")
    add_rom_argument(coverage_gaps)
    coverage_gaps.add_argument("--output", type=Path, default=ROOT / "build/re/coverage-gaps.json")
    coverage_gaps.set_defaults(function=command_coverage_gaps)

    status = commands.add_parser("status", help="show repository and RE progress status")
    add_rom_argument(status)
    status.set_defaults(function=lambda args: print_status(resolve(args.rom)))
    return parser


def main() -> int:
    args = build_parser().parse_args()
    return int(args.function(args))


if __name__ == "__main__":
    raise SystemExit(main())
