"""Manifest-driven MAME experiment and event services."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
from typing import Any

from genie.common import hashes, load_yaml, normalize_symbols, parse_int
from genie.runtime import (
    EVENTS, EXPERIMENTS, EVENT_FORMAT, _event_detector_definitions,
    _readiness_compare, _state_path_value,
    run_shell_tool,
)
from genie.games.aladdin.mame.state import load_state_trace

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

def _event_field(value: Any, label: str, *, allow_empty: bool = False) -> str:
    if value is None:
        raise SystemExit(f"event {label} is required")
    text = str(value)
    if (not text and not allow_empty) or any(character in text for character in "|;~,\\"):
        raise SystemExit(f"event {label} contains a reserved protocol character: {text!r}")
    return text

def event_detector_protocol() -> str | None:
    """Compile passive semantic event detectors into the Lua protocol."""
    document = load_yaml(EVENTS) or {}
    detectors = document.get("detectors") or []
    if not detectors:
        return None
    symbols = _symbol_index()
    operators = {
        "equals": "eq",
        "equal": "eq",
        "not_equals": "ne",
        "less_than": "lt",
        "less_equal": "le",
        "greater_than": "gt",
        "greater_equal": "ge",
    }
    encoded_detectors: list[str] = []
    for detector in detectors:
        detector = detector or {}
        name = _event_field(detector.get("name"), "name")
        event = _event_field(detector.get("event", name), "event")
        phase = _event_field(detector.get("phase", "unknown"), "phase")
        level = _event_field(detector.get("level", ""), "level", allow_empty=True)
        checkpoint = _event_field(detector.get("checkpoint", name), "checkpoint")
        stable_for = int(detector.get("stable_for", 1))
        if stable_for < 1:
            raise SystemExit(f"event detector {name!r} stable_for must be positive")
        emit = detector.get("emit")
        if emit is None:
            # Preserve the old manifest spelling while making its actual
            # behavior explicit: repeatable detectors fire once per stable
            # active interval, while the default fires once per run.
            emit = "every_stable_interval" if detector.get("repeatable", False) else "once"
        emit = str(emit).lower()
        if emit not in {"once", "rising_edge", "every_stable_interval"}:
            raise SystemExit(
                f"event detector {name!r} has unsupported emit mode {emit!r}"
            )
        conditions: list[str] = []
        for condition in detector.get("conditions") or []:
            condition = condition or {}
            if "pc" in condition:
                target_name = str(condition["pc"])
                target = symbols.get(target_name)
                expected = int(target["address"]) if target else parse_int(target_name)
                conditions.append(f"pc,{_event_field(target_name, 'pc')},u32,eq,{expected}")
                continue
            symbol_name = condition.get("symbol")
            if symbol_name is None:
                raise SystemExit(f"event detector {name!r} condition has no symbol or pc")
            symbol_name = str(symbol_name)
            if symbol_name not in symbols:
                raise SystemExit(f"event detector {name!r} references unknown symbol {symbol_name!r}")
            width = str(condition.get("width", condition.get("type", symbols[symbol_name].get("type", "u8")))).lower()
            if width not in ("u8", "u16", "u32", "i16", "s16"):
                raise SystemExit(f"event detector {name!r} has unsupported width {width!r}")
            operation = "eq"
            value: Any = 0
            for key, encoded_operation in operators.items():
                if key in condition:
                    operation = encoded_operation
                    value = condition[key]
                    break
            else:
                raise SystemExit(f"event detector {name!r} condition has no comparison")
            conditions.append(
                f"symbol,{_event_field(symbol_name, 'symbol')},{width},{operation},{parse_int(value)}"
            )
        if not conditions:
            raise SystemExit(f"event detector {name!r} has no conditions")
        encoded_detectors.append("|".join((
            name,
            event,
            phase,
            level,
            checkpoint,
            str(stable_for),
            emit,
            "~".join(conditions),
        )))
    return ";".join(encoded_detectors)

EVENT_STATE_FIELDS = {
    "PLAYER_X": "player.x",
    "PLAYER_Y": "player.y",
    "PLAYER_WORLD_X": "player.world_x",
    "PLAYER_WORLD_Y": "player.world_y",
    "PLAYER_VX": "player.vx",
    "PLAYER_VY": "player.vy",
    "PLAYER_ANIMATION_PC": "player.animation_pc",
    "PLAYER_FRAME_PTR": "player.frame_ptr",
    "PLAYER_ANIMATION_TIMER": "player.animation_timer",
    "TERRAIN_LANDING_STATE": "terrain.landing_state",
    "TERRAIN_BEHAVIOR": "terrain.behavior",
    "SCENE_STATE": "scene.state",
    "WORLD_CAMERA_X": "camera.x",
    "WORLD_CAMERA_Y": "camera.y",
}

def _event_condition_value(state: dict[str, Any], condition: dict[str, Any]) -> Any:
    """Resolve a manifest event predicate against the canonical state view."""
    if "field" in condition:
        field = str(condition["field"])
    elif "symbol" in condition:
        symbol_name = str(condition["symbol"])
        field = EVENT_STATE_FIELDS.get(symbol_name)
        if field is None:
            raise SystemExit(
                f"event condition {symbol_name!r} is not exposed by the semantic state trace"
            )
    elif "pc" in condition:
        return state.get("pc")
    else:
        raise SystemExit(f"event condition has no field, symbol, or pc: {condition}")
    return _state_path_value(state, field)

def _event_condition_compare(actual: Any, condition: dict[str, Any]) -> tuple[bool, dict[str, Any]]:
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
    expected: Any = None
    for key, encoded in operators.items():
        if key in condition:
            operation = encoded
            expected = parse_int(condition[key])
            break
    else:
        raise SystemExit(f"event condition has no comparison: {condition}")
    try:
        actual_value = None if actual is None else int(actual)
    except (TypeError, ValueError) as error:
        raise SystemExit(f"event condition value is not numeric: {actual!r}") from error
    met = _readiness_compare(actual_value, operation, expected)
    return met, {
        "kind": "state",
        "field": str(condition.get("field", condition.get("symbol", condition.get("pc", "pc")))),
        "operation": operation,
        "expected": expected,
        "actual": actual_value,
    }

def derive_events_from_state(
    state_path: Path,
    destination: Path,
    *,
    end_frame: int | None = None,
) -> list[dict[str, Any]]:
    """Interpret synchronized state in Python and write the canonical events."""
    _, states, _ = load_state_trace(state_path)
    definitions = _event_detector_definitions()
    events: list[dict[str, Any]] = []
    runtime: dict[str, dict[str, Any]] = {}
    for name, detector in definitions.items():
        emit = str(detector.get("emit") or (
            "every_stable_interval" if detector.get("repeatable", False) else "once"
        )).lower()
        if emit not in {"once", "rising_edge", "every_stable_interval"}:
            raise SystemExit(f"event detector {name!r} has unsupported emit mode {emit!r}")
        stable_for = int(detector.get("stable_for", 1))
        if stable_for < 1:
            raise SystemExit(f"event detector {name!r} stable_for must be positive")
        runtime[name] = {
            "active_frames": 0,
            "onset_frame": None,
            "emitted": False,
            "emit": emit,
            "stable_for": stable_for,
        }

    for frame in sorted(states):
        if end_frame is not None and frame >= end_frame:
            break
        state = states[frame]
        for name, detector in definitions.items():
            status = runtime[name]
            evidence: list[dict[str, Any]] = []
            active = True
            for condition in detector.get("conditions") or []:
                met, item = _event_condition_compare(
                    _event_condition_value(state, condition), condition
                )
                evidence.append(item)
                active = active and met
            if active:
                if status["active_frames"] == 0:
                    status["onset_frame"] = frame
                status["active_frames"] += 1
            else:
                status["active_frames"] = 0
                status["onset_frame"] = None
                if status["emit"] != "once":
                    status["emitted"] = False
            if active and status["active_frames"] >= status["stable_for"] and not status["emitted"]:
                checkpoint = str(detector.get("checkpoint") or name)
                events.append({
                    "type": "event",
                    "format": EVENT_FORMAT,
                    "frame": frame,
                    "onset_frame": status["onset_frame"],
                    "confirmed_frame": frame,
                    "stable_for": status["stable_for"],
                    "emit": status["emit"],
                    "name": name,
                    "event": str(detector.get("event", name)),
                    "phase": str(detector.get("phase", "unknown")),
                    "level": str(detector.get("level", "")),
                    "checkpoint": checkpoint,
                    "state": "",
                    "evidence": evidence,
                })
                status["emitted"] = True

    destination.parent.mkdir(parents=True, exist_ok=True)
    header = {
        "type": "header",
        "format": EVENT_FORMAT,
        "frame_contract": "S[N] = synchronized state at boundary N; I[N] = input for S[N] -> S[N+1]",
        "frame_semantics": "event predicate E[N] is evaluated against synchronized state S[N]",
        "source_artifact": state_path.name,
        "evaluator": {"name": "openaladdin-python-event-engine", "version": 1},
        "detectors": sorted(definitions),
    }
    with destination.open("w", encoding="utf-8") as output:
        output.write(json.dumps(header, separators=(",", ":")) + "\n")
        for event in events:
            output.write(json.dumps(event, separators=(",", ":")) + "\n")
    return events

def _checkpoint_name(event: dict[str, Any], index: int, used: dict[str, int]) -> str:
    base = str(event.get("checkpoint") or event.get("name") or f"event-{index}")
    used[base] = used.get(base, 0) + 1
    return base if used[base] == 1 else f"{base}-{used[base]}"

def capture_event_checkpoints(
    run_dir: Path,
    rom: Path,
    manifest: dict[str, Any],
    events_path: Path,
) -> int:
    """Materialize Python-derived event boundaries with one fast replay."""
    header, events = load_event_timeline(events_path)
    if not events:
        return 0
    used: dict[str, int] = {}
    checkpoint_items: list[str] = []
    for index, event in enumerate(events):
        name = _checkpoint_name(event, index, used)
        event["checkpoint"] = name
        event["state"] = f"checkpoints/genesis/{name}.sta"
        checkpoint_items.append(f"{int(event['confirmed_frame'])}={name}")
    checkpoint_dir = run_dir / "raw" / "checkpoint-capture"
    checkpoint_dir.mkdir(parents=True, exist_ok=True)
    from genie.games.aladdin.mame.runs import _apply_run_start, _clean_mame_environment, _copy_artifact

    environment = _clean_mame_environment()
    environment.update({
        "OPENALADDIN_TRACE_DIR": str(checkpoint_dir),
        "OPENALADDIN_TRACE_FRAMES": str(max(int(event["confirmed_frame"]) for event in events)),
        "OPENALADDIN_CAPTURE": "none",
        "OPENALADDIN_INPUT_MODE": "playback",
        "OPENALADDIN_PLAYBACK_FILE": str(run_dir / "mame.inp"),
        "OPENALADDIN_CHECKPOINTS": ",".join(checkpoint_items),
        "OPENALADDIN_STATE_DIRECTORY": str(run_dir / "checkpoints"),
        "OPENALADDIN_CHECKPOINT_REFERENCE": "checkpoints",
        "OPENALADDIN_MAME_HEADLESS": "1",
        "OPENALADDIN_MAME_VIDEO": "none",
        "OPENALADDIN_MAME_SOUND": "none",
        "OPENALADDIN_EXECUTION_PROFILE": "analysis",
        "OPENALADDIN_ROM_SHA256": manifest["rom_sha256"],
    })
    _apply_run_start(environment, manifest)
    status = run_shell_tool("mame/run.sh", [str(rom)], env=environment)
    if status != 0:
        raise SystemExit(f"record: event checkpoint replay failed with status {status}")
    for event in events:
        checkpoint = _event_state_path(run_dir, str(event["state"]))
        if not checkpoint.is_file():
            raise SystemExit(f"record: event checkpoint not found: {checkpoint}")
        event["state_sha256"] = hashes(checkpoint)["sha256"]
    with events_path.open("w", encoding="utf-8") as output:
        if header is not None:
            output.write(json.dumps(header, separators=(",", ":")) + "\n")
        for event in events:
            output.write(json.dumps(event, separators=(",", ":")) + "\n")
    _copy_artifact(events_path, run_dir / "events.jsonl")
    return len(events)

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

__all__ = [name for name in globals() if not name.startswith("__")]
