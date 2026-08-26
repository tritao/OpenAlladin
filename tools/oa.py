#!/usr/bin/env python3
"""Unified OpenAladdin reverse-engineering workflow frontend.

This command intentionally delegates the actual work to the existing tools.
The tracked YAML files and the ROM remain the source of truth; this file only
provides one discoverable entry point and a small amount of orchestration.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
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
EVENTS = ROOT / "re/mame/events/manifest.yml"
GHIDRA_CONFIG = ROOT / "re/config/ghidra.yml"
ROM_DEFAULT = ROOT / "rom/Disneys_Aladdin_U_p1.bin"

INPUT_FORMAT = "openaladdin-input-v1"
INPUT_MAPPING = "mame-genesis-3button-v1"
LEGACY_BUTTON_REMAP = {"a": "b", "b": "c", "c": "a"}
EVENT_FORMAT = "openaladdin-event-v1"
SEGMENTS_FORMAT = "openaladdin-segments-v1"
RUN_FORMAT = "openaladdin-input-run-v1"
INPUT_BUTTONS = ("up", "down", "left", "right", "a", "b", "c", "start")
INPUT_MASKS = {name: 1 << index for index, name in enumerate(INPUT_BUTTONS)}
DEFAULT_PARITY_FIELDS = [
    "player.x",
    "player.y",
    "player.world_x",
    "player.world_y",
    "player.vx",
    "player.vy",
    "player.animation_pc",
    "player.frame_ptr",
    "player.facing_x_flip",
    "player.animation_timer",
    "player.grounded",
    "scene.state",
    "camera.x",
    "camera.y",
    "camera.scroll_x",
    "camera.scroll_y",
    "actors",
]


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


def _relative_to_root(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(ROOT))
    except ValueError:
        return str(path.resolve())


def _git_revision(path: Path) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return ""
    return result.stdout.strip() if result.returncode == 0 else ""


def _write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def run_directory(name: str) -> Path:
    """Resolve a user-facing run name without allowing path escape."""
    if not name or Path(name).is_absolute():
        raise SystemExit("run name must be a non-empty relative path")
    base = (ROOT / "build/runs").resolve()
    directory = (base / name).resolve()
    if directory != base and base not in directory.parents:
        raise SystemExit(f"run name escapes {base}: {name!r}")
    return directory


def buttons_for_mask(mask: int) -> list[str]:
    if mask < 0 or mask > 0xFF:
        raise SystemExit(f"input mask out of range: {mask}")
    return [name for name in INPUT_BUTTONS if mask & INPUT_MASKS[name]]


def token_for_mask(mask: int) -> str:
    buttons = buttons_for_mask(mask)
    return "+".join(buttons) if buttons else "none"


def _client_input_tokens(
    header: dict[str, Any] | None,
    tokens: list[str],
) -> list[str]:
    """Translate pre-mapping-v1 recordings for replay clients.

    Early recordings labelled the raw Mega Drive bits linearly as A/B/C.
    MAME's actual fields are B/C/A at those bit positions. New recordings
    carry ``controller_mapping`` and need no translation; a missing mapping is
    retained as a backward-compatible legacy marker.
    """
    if header and header.get("controller_mapping") is not None:
        return list(tokens)
    translated: list[str] = []
    for token in tokens:
        if token == "none":
            translated.append(token)
            continue
        translated.append("+".join(
            LEGACY_BUTTON_REMAP.get(part, part)
            for part in token.split("+")
        ))
    return translated


def load_input_timeline(path: Path) -> tuple[dict[str, Any] | None, list[dict[str, Any]]]:
    header: dict[str, Any] | None = None
    records: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise SystemExit(f"{path}:{line_number}: invalid JSON: {error}") from error
        if not isinstance(record, dict):
            raise SystemExit(f"{path}:{line_number}: input record must be an object")
        if record.get("type") == "header":
            header = record
            if header.get("format") not in (None, INPUT_FORMAT):
                raise SystemExit(f"{path}: unsupported input format {header.get('format')!r}")
            mapping = header.get("controller_mapping")
            if mapping not in (None, INPUT_MAPPING):
                raise SystemExit(f"{path}: unsupported controller mapping {mapping!r}")
            continue
        if "frame" not in record or "mask" not in record:
            raise SystemExit(f"{path}:{line_number}: input record requires frame and mask")
        frame = int(record["frame"])
        if frame != len(records):
            raise SystemExit(
                f"{path}:{line_number}: expected frame {len(records)}, got {frame}"
            )
        mask = int(record["mask"])
        expected_buttons = buttons_for_mask(mask)
        buttons = record.get("buttons")
        if buttons is not None and list(buttons) != expected_buttons:
            raise SystemExit(
                f"{path}:{line_number}: mask/buttons mismatch: {mask} != {buttons!r}"
            )
        records.append({"frame": frame, "mask": mask, "buttons": expected_buttons})
    if not records:
        raise SystemExit(f"{path}: input timeline has no frame records")
    return header, records


def load_event_timeline(path: Path) -> tuple[dict[str, Any] | None, list[dict[str, Any]]]:
    """Load the low-volume semantic events emitted by the MAME harness."""
    header: dict[str, Any] | None = None
    events: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise SystemExit(f"{path}:{line_number}: invalid JSON: {error}") from error
        if not isinstance(record, dict):
            raise SystemExit(f"{path}:{line_number}: event record must be an object")
        if record.get("type") == "header":
            header = record
            if header.get("format") not in (None, EVENT_FORMAT):
                raise SystemExit(f"{path}: unsupported event format {header.get('format')!r}")
            continue
        if record.get("type") != "event":
            raise SystemExit(f"{path}:{line_number}: expected an event record")
        if "frame" not in record or "name" not in record:
            raise SystemExit(f"{path}:{line_number}: event requires frame and name")
        events.append(record)
    return header, events


def _event_state_path(run_dir: Path, reference: str) -> Path:
    path = Path(reference)
    return path if path.is_absolute() else run_dir / path


def load_segments(run_dir: Path) -> list[dict[str, Any]]:
    """Load the derived event segments for a recorded run."""
    path = run_dir / "segments.json"
    if not path.is_file():
        raise SystemExit(f"segment index not found: {path}; re-record the run")
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise SystemExit(f"{path}: invalid JSON: {error}") from error
    if document.get("format") != SEGMENTS_FORMAT:
        raise SystemExit(f"{path}: unsupported segment format {document.get('format')!r}")
    segments = document.get("segments")
    if not isinstance(segments, list):
        raise SystemExit(f"{path}: segments must be an array")
    return [dict(segment) for segment in segments]


def _segment_slug(segment_id: str) -> str:
    """Return a filesystem-safe derived directory name for a segment id."""
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", segment_id):
        raise SystemExit(f"unsafe segment id: {segment_id!r}")
    return segment_id


def select_segment(run_dir: Path, segment_id: str) -> dict[str, Any]:
    segments = load_segments(run_dir)
    for segment in segments:
        if str(segment.get("id", "")) == segment_id:
            try:
                start_frame = int(segment["start_frame"])
                end_frame = int(segment["end_frame"])
            except (KeyError, TypeError, ValueError) as error:
                raise SystemExit(f"invalid segment {segment_id!r} in {run_dir / 'segments.json'}") from error
            if start_frame < 0 or end_frame < start_frame:
                raise SystemExit(f"invalid frame range for segment {segment_id!r}")
            selected = dict(segment)
            selected["start_frame"] = start_frame
            selected["end_frame"] = end_frame
            _backfill_native_boundary(run_dir, selected)
            native_start_frame = selected.get("native_start_frame")
            if native_start_frame is not None:
                try:
                    native_start_frame = int(native_start_frame)
                except (TypeError, ValueError) as error:
                    raise SystemExit(
                        f"invalid native_start_frame for segment {segment_id!r}"
                    ) from error
                if native_start_frame < start_frame or native_start_frame > end_frame:
                    raise SystemExit(f"invalid native frame range for segment {segment_id!r}")
                selected["native_start_frame"] = native_start_frame
            selected["slug"] = _segment_slug(segment_id)
            return selected
    known = ", ".join(str(segment.get("id")) for segment in segments) or "none"
    raise SystemExit(f"unknown segment {segment_id!r}; known segments: {known}")


def _state_int(mapping: dict[str, Any], name: str, default: int = 0) -> int:
    value = mapping.get(name, default)
    if value is None:
        return default
    try:
        return int(value)
    except (TypeError, ValueError) as error:
        raise SystemExit(f"state field {name!r} is not an integer: {value!r}") from error


def _state_bool(mapping: dict[str, Any], name: str, default: bool = False) -> bool:
    value = mapping.get(name, default)
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        if value.lower() in {"true", "yes", "on", "1"}:
            return True
        if value.lower() in {"false", "no", "off", "0", ""}:
            return False
    return bool(value)


_READINESS_OPERATORS = {
    "equals": "eq",
    "equal": "eq",
    "not_equals": "ne",
    "less_than": "lt",
    "less_equal": "le",
    "greater_than": "gt",
    "greater_equal": "ge",
}
_READINESS_EXPRESSION_OPERATORS = {
    "==": "eq",
    "!=": "ne",
    "<": "lt",
    "<=": "le",
    ">": "gt",
    ">=": "ge",
}
_READINESS_OPERAND = r"(?:-?(?:0[xX][0-9A-Fa-f]+|\d+)|[A-Za-z_][A-Za-z0-9_.]*)"


def _state_path_value(state: dict[str, Any], path: str) -> Any:
    """Read a dotted value from a captured semantic state record."""
    value: Any = state
    for part in path.split("."):
        if not isinstance(value, dict) or part not in value:
            return None
        value = value[part]
    return value


def _readiness_operand(state: dict[str, Any], operand: str) -> int | None:
    operand = operand.strip()
    if re.fullmatch(r"-?(?:0[xX][0-9A-Fa-f]+|\d+)", operand):
        return parse_int(operand)
    value = _state_path_value(state, operand)
    if value is None:
        return None
    try:
        return int(value)
    except (TypeError, ValueError) as error:
        raise SystemExit(
            f"native readiness field {operand!r} is not numeric: {value!r}"
        ) from error


def _readiness_expression_value(state: dict[str, Any], expression: str) -> int | None:
    """Evaluate the deliberately small arithmetic subset used by readiness rules."""
    position = 0
    operand_match = re.match(rf"\s*({_READINESS_OPERAND})", expression)
    if not operand_match:
        raise SystemExit(f"unsupported native readiness expression: {expression!r}")
    result = _readiness_operand(state, operand_match.group(1))
    position = operand_match.end()
    while position < len(expression):
        operator_match = re.match(r"\s*([+-])\s*", expression[position:])
        if not operator_match:
            raise SystemExit(f"unsupported native readiness expression: {expression!r}")
        operand_match = re.match(
            rf"\s*({_READINESS_OPERAND})", expression[position + operator_match.end():]
        )
        if not operand_match:
            raise SystemExit(f"unsupported native readiness expression: {expression!r}")
        value = _readiness_operand(state, operand_match.group(1))
        if result is None or value is None:
            result = None
        elif operator_match.group(1) == "+":
            result += value
        else:
            result -= value
        position += operator_match.end() + operand_match.end()
    return result


def _readiness_compare(actual: int | None, operation: str, expected: int | None) -> bool:
    if actual is None or expected is None:
        return False
    if operation == "ne":
        return actual != expected
    if operation == "lt":
        return actual < expected
    if operation == "le":
        return actual <= expected
    if operation == "gt":
        return actual > expected
    if operation == "ge":
        return actual >= expected
    return actual == expected


def _native_readiness_condition(
    state: dict[str, Any], condition: dict[str, Any]
) -> tuple[bool, dict[str, Any]]:
    """Evaluate one manifest-declared predicate and retain its evidence."""
    if "derived" in condition:
        expression = str(condition["derived"])
        match = re.fullmatch(rf"\s*(.+?)\s*(==|!=|<=|>=|<|>)\s*(.+?)\s*", expression)
        if not match:
            raise SystemExit(f"unsupported native readiness expression: {expression!r}")
        left, operator, right = match.groups()
        actual = _readiness_expression_value(state, left)
        expected = _readiness_expression_value(state, right)
        operation = _READINESS_EXPRESSION_OPERATORS[operator]
        return _readiness_compare(actual, operation, expected), {
            "kind": "derived",
            "expression": expression,
            "operation": operation,
            "actual": actual,
            "expected": expected,
        }

    field = condition.get("field")
    if field is None:
        raise SystemExit("native readiness condition requires derived or field")
    field = str(field)
    actual_value = _state_path_value(state, field)
    actual = None if actual_value is None else int(actual_value)
    operation = "eq"
    expected: int | None = None
    expected_field: str | None = None
    for key, encoded_operation in _READINESS_OPERATORS.items():
        if key in condition:
            operation = encoded_operation
            raw_expected = condition[key]
            if key in ("equals", "equal") and isinstance(raw_expected, str) and "." in raw_expected:
                expected_field = raw_expected
                expected_value = _state_path_value(state, raw_expected)
                expected = None if expected_value is None else int(expected_value)
            else:
                expected = parse_int(raw_expected)
            break
    else:
        raise SystemExit(f"native readiness field condition has no comparison: {condition}")
    evidence: dict[str, Any] = {
        "kind": "field",
        "field": field,
        "operation": operation,
        "actual": actual,
        "expected": expected,
    }
    if expected_field is not None:
        evidence["expected_field"] = expected_field
    return _readiness_compare(actual, operation, expected), evidence


def _event_detector_definitions() -> dict[str, dict[str, Any]]:
    document = load_yaml(EVENTS) or {}
    return {
        str(detector.get("name")): dict(detector)
        for detector in document.get("detectors") or []
        if detector.get("name") is not None
    }


def _find_native_start(
    state_records: dict[int, dict[str, Any]],
    start_frame: int,
    end_frame: int,
    detector: dict[str, Any] | None,
) -> tuple[int | None, dict[str, Any]]:
    """Find the first stable native-representable frame in a segment."""
    config = (detector or {}).get("native_ready")
    if not config:
        return None, {
            "status": "unconfigured",
            "reason": "detector has no native_ready declaration",
        }
    if not isinstance(config, dict):
        raise SystemExit("native_ready must be a mapping")
    stable_for = int(config.get("stable_for", 1))
    if stable_for < 1:
        raise SystemExit("native_ready stable_for must be positive")
    conditions = config.get("conditions") or []
    if not conditions:
        raise SystemExit("native_ready requires at least one condition")
    if not isinstance(conditions, list):
        raise SystemExit("native_ready conditions must be a list")

    for candidate in range(start_frame, end_frame - stable_for + 2):
        stable_frames = list(range(candidate, candidate + stable_for))
        if any(frame not in state_records for frame in stable_frames):
            continue
        all_met = True
        for frame in stable_frames:
            state = state_records[frame]
            for condition in conditions:
                if not isinstance(condition, dict):
                    raise SystemExit("native_ready conditions must be mappings")
                met, _ = _native_readiness_condition(state, condition)
                if not met:
                    all_met = False
                    break
            if not all_met:
                break
        if all_met:
            evidence: list[dict[str, Any]] = []
            for condition in conditions:
                _, condition_evidence = _native_readiness_condition(
                    state_records[candidate], condition
                )
                evidence.append(condition_evidence)
            return candidate, {
                "status": "ready",
                "reason": str(config.get(
                    "reason",
                    "native readiness criteria satisfied for the configured stable window",
                )),
                "stable_for": stable_for,
                "stable_frames": stable_frames,
                "conditions": evidence,
            }
    return None, {
        "status": "unavailable",
        "reason": "native readiness criteria were not satisfied before the segment ended",
        "stable_for": stable_for,
        "conditions": [dict(condition) for condition in conditions],
    }


def _infer_native_animation_phase(
    state_records: dict[int, dict[str, Any]],
    start_frame: int,
    end_frame: int,
) -> dict[str, Any]:
    """Infer the native VM's initial scheduler delay from MAME's trace.

    The native ROM VM is serviced on alternating update passes. Its private
    scheduler phase is not a Genesis RAM field, so a portable checkpoint must
    carry this derived adjustment separately from PLAYER_ANIMATION_TIMER.
    Only infer it while the initial animation cursor and input are stable; an
    input-triggered cursor change is gameplay, not scheduler evidence.
    """
    initial = state_records.get(start_frame) or {}
    initial_player = initial.get("player") or {}
    initial_pc = initial_player.get("animation_pc")
    if initial_pc is None or int(initial_pc) == 0:
        return {
            "status": "not_observable",
            "reason": "initial animation cursor is not available",
            "delay_ticks": 0,
        }
    initial_input = initial.get("input", "none")
    first_change: int | None = None
    for frame in range(start_frame + 1, end_frame + 1):
        state = state_records.get(frame)
        if state is None:
            break
        if state.get("input", "none") != initial_input:
            return {
                "status": "not_observable",
                "reason": "input changed before an isolated animation-cursor transition",
                "delay_ticks": 0,
            }
        player = state.get("player") or {}
        if player.get("animation_pc") != initial_pc:
            first_change = frame
            break
    if first_change is None:
        return {
            "status": "not_observable",
            "reason": "animation cursor did not transition during the segment",
            "delay_ticks": 0,
        }

    offset = first_change - start_frame
    tick_period = 2
    # set_checkpoint() performs one initial animation selection only for an
    # airborne player. A grounded checkpoint reaches its first native VM tick
    # one frame earlier, so the phase model must retain that state distinction.
    initial_tick_offset = 1 if bool(initial_player.get("grounded")) else 2
    if offset < initial_tick_offset or (offset - initial_tick_offset) % tick_period:
        return {
            "status": "unrepresentable",
            "reason": "animation transition does not align with the native VM tick phase",
            "initial_animation_pc": int(initial_pc),
            "first_change_frame": first_change,
            "first_change_offset": offset,
            "initial_tick_offset": initial_tick_offset,
            "native_tick_period": tick_period,
        }
    return {
        "status": "inferred",
        "reason": "animation scheduler delay inferred from the first isolated cursor transition",
        "delay_ticks": (offset - initial_tick_offset) // tick_period,
        "initial_animation_pc": int(initial_pc),
        "first_change_frame": first_change,
        "first_change_offset": offset,
        "initial_tick_offset": initial_tick_offset,
        "native_tick_period": tick_period,
    }


def _backfill_native_boundary(run_dir: Path, segment: dict[str, Any]) -> None:
    """Derive the new native boundary for a pre-readiness segments.json."""
    if "native_start_frame" in segment:
        return
    state_path = run_dir / "state.jsonl"
    if not state_path.is_file():
        return
    _, state_records, _ = load_state_trace(state_path)
    native_start_frame, native_ready = _find_native_start(
        state_records,
        int(segment["start_frame"]),
        int(segment["end_frame"]),
        _event_detector_definitions().get(str(segment.get("detector", ""))),
    )
    segment["native_start_frame"] = native_start_frame
    segment["native_start_reason"] = native_ready["reason"]
    segment["native_ready"] = native_ready
    segment["native_start"] = (
        native_checkpoint_descriptor(state_records[native_start_frame])
        if native_start_frame is not None else None
    )
    segment["native_animation_phase"] = (
        _infer_native_animation_phase(
            state_records,
            native_start_frame,
            int(segment["end_frame"]),
        )
        if native_start_frame is not None else {
            "status": "unavailable",
            "reason": "no stable native boundary was found",
        }
    )


def native_checkpoint_descriptor(state: dict[str, Any]) -> dict[str, Any]:
    """Extract the portable native checkpoint subset from a MAME state."""
    player = state.get("player") or {}
    camera = state.get("camera") or {}
    scene = state.get("scene") or {}
    terrain = state.get("terrain") or {}
    descriptor: dict[str, Any] = {
        "player": {
            name: _state_int(player, name)
            for name in ("x", "y", "world_x", "world_y", "vx", "vy")
        },
        "camera": {
            name: _state_int(camera, name)
            for name in (
                "x",
                "y",
                "reference_x",
                "reference_y",
                "scroll_x",
                "scroll_y",
            )
        },
        "scene": {"state": _state_int(scene, "state", 1)},
    }
    descriptor["player"]["grounded"] = _state_bool(
        player,
        "grounded",
        _state_int(player, "vy") == 0,
    )
    for name in ("frame_ptr", "animation_pc", "animation_timer", "facing_x_flip"):
        if name in player:
            descriptor["player"][name] = _state_int(player, name)
    selector = player.get("animation_selector")
    if isinstance(selector, dict):
        descriptor["player"]["animation_selector"] = {
            name: _state_int(selector, name)
            for name in ANIMATION_SELECTOR_FIELDS
            if name in selector
        }
    if any(name in terrain for name in ("behavior", "landing_state")):
        descriptor["terrain"] = {
            name: _state_int(terrain, name)
            for name in ("behavior", "landing_state")
            if name in terrain
        }
    for name in ("horizontal_threshold", "vertical_threshold", "update_delay"):
        if name in camera:
            descriptor["camera"][name] = _state_int(camera, name)
    return descriptor


def native_checkpoint_arguments(
    checkpoint: dict[str, Any],
    *,
    include_terrain_behavior: bool = True,
    animation_phase_delay: int | None = None,
) -> list[str]:
    """Translate a state or native_start descriptor into native CLI flags."""
    player = checkpoint.get("player") or {}
    camera = checkpoint.get("camera") or {}
    scene = checkpoint.get("scene") or {}
    if not scene and "scene_state" in checkpoint:
        scene = {"state": checkpoint["scene_state"]}

    arguments: list[str] = []
    if player:
        player_spec = ",".join(
            str(_state_int(player, name)) for name in ("x", "y", "vx", "vy")
        )
        grounded = _state_bool(player, "grounded", _state_int(player, "vy") == 0)
        arguments.extend(["--checkpoint-player", f"{player_spec},{1 if grounded else 0}"])
    if camera:
        camera_spec = ",".join(
            str(_state_int(camera, name))
            for name in ("x", "y", "reference_x", "reference_y", "scroll_x", "scroll_y")
        )
        camera_spec += "," + str(_state_int(scene, "state", 1))
        if all(name in camera for name in ("horizontal_threshold", "vertical_threshold", "update_delay")):
            camera_spec += "," + ",".join(
                str(_state_int(camera, name))
                for name in ("horizontal_threshold", "vertical_threshold", "update_delay")
            )
        arguments.extend(["--checkpoint-camera", camera_spec])
    terrain = checkpoint.get("terrain") or {}
    if include_terrain_behavior and "behavior" in terrain:
        arguments.extend(["--checkpoint-terrain-behavior", str(_state_int(terrain, "behavior"))])
    if "landing_state" in terrain:
        arguments.extend([
            "--checkpoint-terrain-landing-state",
            str(_state_int(terrain, "landing_state")),
        ])
    if player.get("frame_ptr"):
        arguments.extend(["--checkpoint-frame-ptr", str(_state_int(player, "frame_ptr"))])
    if player.get("animation_pc"):
        arguments.extend([
            "--checkpoint-animation",
            f"{_state_int(player, 'animation_pc')},{_state_int(player, 'animation_timer')}",
        ])
    if animation_phase_delay is not None:
        if animation_phase_delay < 0:
            raise SystemExit("native animation phase delay must be non-negative")
        arguments.extend([
            "--checkpoint-animation-phase-delay",
            str(animation_phase_delay),
        ])
    selector_spec = animation_selector_spec(player)
    if selector_spec is not None:
        arguments.extend(["--checkpoint-animation-selector", selector_spec])
    if "facing_x_flip" in player:
        arguments.extend(["--checkpoint-facing-x-flip", str(_state_int(player, "facing_x_flip"))])
    return arguments


def _write_sliced_input(
    path: Path,
    header: dict[str, Any] | None,
    records: list[dict[str, Any]],
    start_frame: int,
    end_frame: int,
    segment_id: str,
) -> None:
    if start_frame < 0 or end_frame < start_frame or end_frame >= len(records):
        raise SystemExit(
            f"input slice {segment_id!r} is outside the timeline: "
            f"{start_frame}..{end_frame} / {len(records)}"
        )
    sliced_header = dict(header or {
        "type": "header",
        "format": INPUT_FORMAT,
        "buttons": list(INPUT_BUTTONS),
    })
    sliced_header["frame_limit"] = end_frame - start_frame
    sliced_header["segment"] = segment_id
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as output:
        output.write(json.dumps(sliced_header, separators=(",", ":")) + "\n")
        for relative_frame, record in enumerate(records[start_frame:end_frame + 1]):
            sliced = dict(record)
            sliced["frame"] = relative_frame
            output.write(json.dumps(sliced, separators=(",", ":")) + "\n")


def _write_sliced_state(
    source: Path,
    destination: Path,
    start_frame: int,
    end_frame: int,
    segment_id: str,
    input_tokens: list[str] | None = None,
) -> dict[str, Any]:
    header, states, markers = load_state_trace(source)
    selected_frames = list(range(start_frame, end_frame + 1))
    missing = [frame for frame in selected_frames if frame not in states]
    if missing:
        raise SystemExit(f"{source}: segment {segment_id!r} has missing state frame {missing[0]}")
    sliced_header = dict(header)
    sliced_header["frame_limit"] = end_frame - start_frame
    sliced_header["segment"] = {
        "id": segment_id,
        "source_start_frame": start_frame,
        "source_end_frame": end_frame,
    }
    destination.parent.mkdir(parents=True, exist_ok=True)
    markers_by_frame: dict[int, list[dict[str, Any]]] = {}
    for marker in markers:
        source_frame = int(marker.get("frame", -1))
        if start_frame <= source_frame <= end_frame:
            rebased = dict(marker)
            rebased["frame"] = source_frame - start_frame
            markers_by_frame.setdefault(source_frame - start_frame, []).append(rebased)
    with destination.open("w", encoding="utf-8") as output:
        output.write(json.dumps(sliced_header, separators=(",", ":")) + "\n")
        for relative_frame, source_frame in enumerate(selected_frames):
            record = json.loads(json.dumps(states[source_frame]))
            record["type"] = "state"
            record["frame"] = relative_frame
            if input_tokens is not None:
                if relative_frame >= len(input_tokens):
                    raise SystemExit(
                        f"{source}: segment {segment_id!r} has no replay input "
                        f"for relative frame {relative_frame}"
                    )
                # Direct field injection is installed at the frame boundary
                # after the state sample, so the raw MAME callback reports the
                # next token while the emulated state already reflects the
                # current one. Rebase the derived replay trace to the
                # canonical consumed-frame convention.
                record["input"] = input_tokens[relative_frame]
            output.write(json.dumps(record, separators=(",", ":")) + "\n")
            for marker in markers_by_frame.get(relative_frame, []):
                output.write(json.dumps(marker, separators=(",", ":")) + "\n")
    return states[start_frame]


def _rebase_state_trace_in_place(
    path: Path,
    start_frame: int,
    end_frame: int,
    segment_id: str,
    input_tokens: list[str] | None = None,
) -> None:
    """Remove the startup sample before a Lua-preloaded MAME state."""
    temporary = path.with_name(path.name + ".rebased")
    _write_sliced_state(
        path,
        temporary,
        start_frame,
        end_frame,
        segment_id,
        input_tokens=input_tokens,
    )
    temporary.replace(path)


def _relabel_state_trace_inputs(
    path: Path,
    input_tokens: list[str],
    trace_name: str,
) -> None:
    """Relabel a derived replay trace to its source timeline convention."""
    _, states, _ = load_state_trace(path)
    if not states:
        return
    start_frame = min(states)
    end_frame = max(states)
    if end_frame >= len(input_tokens):
        raise SystemExit(
            f"{path}: replay trace frame {end_frame} has no source input token"
        )
    temporary = path.with_name(path.name + ".inputs")
    _write_sliced_state(
        path,
        temporary,
        start_frame,
        end_frame,
        trace_name,
        input_tokens=input_tokens[start_frame:end_frame + 1],
    )
    temporary.replace(path)


def _write_segments(run_dir: Path, frame_count: int) -> tuple[int, int]:
    """Materialize semantic event boundaries as derived parity segments."""
    events_path = run_dir / "events.jsonl"
    if not events_path.is_file():
        raise SystemExit(f"record: missing {events_path}")
    _, events = load_event_timeline(events_path)
    segments: list[dict[str, Any]] = []
    state_path = run_dir / "state.jsonl"
    state_records: dict[int, dict[str, Any]] = {}
    if events and not state_path.is_file():
        raise SystemExit(f"record: missing {state_path} needed for event checkpoints")
    if events:
        _, state_records, _ = load_state_trace(state_path)
    detector_definitions = _event_detector_definitions()
    used_ids: dict[str, int] = {}
    for index, event in enumerate(events):
        event_frame = int(event["frame"])
        if event_frame < 0 or event_frame >= frame_count:
            raise SystemExit(f"event frame {event_frame} is outside the {frame_count}-frame input timeline")
        # events.poll runs after the state sample at N. The MAME checkpoint
        # saved by the detector is therefore the replayable boundary for the
        # following logical frame. Keep both values explicit in the index.
        start_frame = event_frame + 1
        next_event_frame = int(events[index + 1]["frame"]) if index + 1 < len(events) else frame_count
        if start_frame >= frame_count:
            raise SystemExit(f"event frame {event_frame} has no following replayable state frame")
        segment_id = str(event["name"])
        used_ids[segment_id] = used_ids.get(segment_id, 0) + 1
        if used_ids[segment_id] > 1:
            segment_id = f"{segment_id}-{used_ids[str(event['name'])]}"
        state_reference = str(event.get("state", ""))
        event_state_path = _event_state_path(run_dir, state_reference) if state_reference else None
        if start_frame not in state_records:
            raise SystemExit(f"{run_dir / 'state.jsonl'}: segment start frame {start_frame} has no state record")
        native_start_frame, native_ready = _find_native_start(
            state_records,
            start_frame,
            max(start_frame, min(frame_count - 1, next_event_frame - 1)),
            detector_definitions.get(str(event.get("name", ""))),
        )
        native_end_frame = max(start_frame, min(frame_count - 1, next_event_frame - 1))
        segment: dict[str, Any] = {
            "id": segment_id,
            "event_frame": event_frame,
            "start_frame": start_frame,
            "end_frame": max(start_frame, min(frame_count - 1, next_event_frame - 1)),
            "event": str(event.get("event", "")),
            "detector": str(event.get("name", "")),
            "phase": str(event.get("phase", "unknown")),
            "level": str(event.get("level", "")),
            "checkpoint": str(event.get("checkpoint", "")),
            "mame_state": state_reference,
            "native_start_frame": native_start_frame,
            "native_start_reason": native_ready["reason"],
            "native_ready": native_ready,
            "native_start": (
                native_checkpoint_descriptor(state_records[native_start_frame])
                if native_start_frame is not None else None
            ),
            "native_animation_phase": (
                _infer_native_animation_phase(
                    state_records,
                    native_start_frame,
                    native_end_frame,
                )
                if native_start_frame is not None else {
                    "status": "unavailable",
                    "reason": "no stable native boundary was found",
                }
            ),
        }
        if event_state_path and event_state_path.is_file():
            segment["mame_state_sha256"] = hashes(event_state_path)["sha256"]
        elif event_state_path:
            raise SystemExit(f"event checkpoint not found: {event_state_path}")
        segments.append(segment)
    _write_json(run_dir / "segments.json", {
        "format": SEGMENTS_FORMAT,
        "source": "events.jsonl",
        "frame_count": frame_count,
        "frame_semantics": "segment frame N is the controller state consumed by logical game frame N",
        "boundary_semantics": (
            "event_frame is the detector sample; start_frame is the first "
            "replayable post-event state and MAME checkpoint boundary; "
            "native_start_frame is the first stable state satisfying the "
            "detector's native_ready criteria"
        ),
        "segments": segments,
    })
    return len(events), len(segments)


def input_tokens(records: list[dict[str, Any]]) -> list[str]:
    return [token_for_mask(int(record["mask"])) for record in records]


def readable_input_schedule(tokens: list[str]) -> str:
    if not tokens:
        return ""
    parts: list[str] = []
    start = 0
    while start < len(tokens):
        end = start + 1
        while end < len(tokens) and tokens[end] == tokens[start]:
            end += 1
        count = end - start
        parts.append(tokens[start] if count == 1 else f"{tokens[start]}*{count}")
        start = end
    return ",".join(parts)


def _record_start(args: argparse.Namespace) -> dict[str, Any]:
    if not args.load_state:
        return {"type": "power_on"}
    state = resolve(Path(args.load_state))
    result: dict[str, Any] = {
        "type": "save_state",
        "name": state.stem,
        "path": _relative_to_root(state),
    }
    if state.is_file():
        result["sha256"] = hashes(state)["sha256"]
    return result


def _new_run_manifest(args: argparse.Namespace, run_dir: Path, rom: Path) -> dict[str, Any]:
    return {
        "format": RUN_FORMAT,
        "name": args.name,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "rom": rom.name,
        "rom_path": _relative_to_root(rom),
        "rom_sha256": hashes(rom)["sha256"],
        "repository_commit": _git_revision(ROOT),
        "mame_commit": _git_revision(ROOT / "external/mame"),
        "controller": args.controller,
        "start": _record_start(args),
        "frames": 0,
        "status": "recording",
        "input_format": INPUT_FORMAT,
        "input_mapping": INPUT_MAPPING,
        "event_format": EVENT_FORMAT,
        "segments_format": SEGMENTS_FORMAT,
        "frame_semantics": (
            "frame N is the controller state consumed by logical game frame N"
        ),
        "artifacts": {
            "input": "input.jsonl",
            "state": "state.jsonl",
            "events": "events.jsonl",
            "segments": "segments.json",
            "mame_input": "mame.inp",
            "checkpoints": "checkpoints/",
        },
        "replays": {},
    }


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
        once = "0" if detector.get("repeatable", False) else "1"
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
            once,
            "~".join(conditions),
        )))
    return ";".join(encoded_detectors)


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
        "OPENALADDIN_EVENT_OUTPUT",
        "OPENALADDIN_EVENT_SPEC",
        "OPENALADDIN_STATE_OUTPUT",
        "OPENALADDIN_CAPTURE",
        "OPENALADDIN_TRACE_ACTORS",
        "OPENALADDIN_LOAD_STATE",
        "OPENALADDIN_PRELOAD_BEFORE_CAPTURE",
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
    r"frameptr=(?P<frame_ptr>[0-9A-F]+) facing=(?P<facing_x_flip>[0-9A-F]+) "
    r"animpc=(?P<animation_pc>[0-9A-F]+) "
    r"animtimer=(?P<animation_timer>[0-9A-F]+) "
    r"camx=(?P<camera_x>[0-9A-F]+) camy=(?P<camera_y>[0-9A-F]+) "
    r"refx=(?P<reference_x>[0-9A-F]+) refy=(?P<reference_y>[0-9A-F]+) "
    r"sx=(?P<scroll_x>[0-9A-F]+) sy=(?P<scroll_y>[0-9A-F]+) "
    r"thx=(?P<horizontal_threshold>[0-9A-F]+) "
    r"thy=(?P<vertical_threshold>[0-9A-F]+) "
    r"delay=(?P<update_delay>[0-9A-F]+) special=(?P<special_mode>[0-9A-F]+) "
    r"selgate=(?P<animation_gate>[0-9A-F]+) "
    r"selterminal=(?P<terminal_transition>[0-9A-F]+) "
    r"selcountdown=(?P<scene_script_countdown>[0-9A-F]+) "
    r"sellock=(?P<interaction_lock>[0-9A-F]+) "
    r"selresponse=(?P<response_active>[0-9A-F]+) "
    r"sellanding=(?P<landing_state>[0-9A-F]+) "
    r"selgate2=(?P<transition_gate>[0-9A-F]+) "
    r"seltranslock=(?P<transition_lock>[0-9A-F]+) "
    r"selstate=(?P<transition_state>[0-9A-F]+) "
    r"selmode=(?P<transition_mode>[0-9A-F]+) "
    r"selflag=(?P<transition_flag>[0-9A-F]+) "
    r"seltransresponse=(?P<transition_response>[0-9A-F]+) "
    r"selde=(?P<transition_state_de>[0-9A-F]+) "
    r"seldf=(?P<transition_state_df>[0-9A-F]+) "
    r"selspecial=(?P<camera_special_mode>[0-9A-F]+) "
    r"sellatch=(?P<response_latch>[0-9A-F]+) "
    r"selanimation=(?P<response_animation>[0-9A-F]+) "
    r"selee=(?P<response_state_ee>[0-9A-F]+) "
    r"selef=(?P<response_state_ef>[0-9A-F]+) "
    r"self0=(?P<response_state_f0>[0-9A-F]+) "
    r"sel101=(?P<response_state_101>[0-9A-F]+) "
    r"selhresponse=(?P<horizontal_response>[0-9A-F]+) "
    r"seltimer=(?P<response_timer>[0-9A-F]+) "
    r"selpending=(?P<interaction_pending>[0-9A-F]+) "
    r"sellock2=(?P<state_lock>[0-9A-F]+)"
)


def _signed_u16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


ANIMATION_SELECTOR_FIELDS = (
    "animation_gate",
    "terminal_transition",
    "scene_script_countdown",
    "interaction_lock",
    "response_active",
    "landing_state",
    "transition_gate",
    "transition_lock",
    "transition_state",
    "transition_mode",
    "transition_flag",
    "transition_response",
    "transition_state_de",
    "transition_state_df",
    "camera_special_mode",
    "response_latch",
    "response_animation",
    "response_state_ee",
    "response_state_ef",
    "response_state_f0",
    "response_state_101",
    "horizontal_response",
    "response_timer",
    "interaction_pending",
    "state_lock",
)


def animation_selector_spec(player: dict[str, Any]) -> str | None:
    selector = player.get("animation_selector")
    if not isinstance(selector, dict):
        return None
    if any(field not in selector for field in ANIMATION_SELECTOR_FIELDS):
        return None
    return ",".join(str(int(selector[field])) for field in ANIMATION_SELECTOR_FIELDS)


def _sync_record(match: re.Match[str]) -> dict[str, int]:
    values = {
        name: int(value, 10 if name == "frame" else 16)
        for name, value in match.groupdict().items()
    }
    values["vx"] = _signed_u16(values["vx"])
    values["vy"] = _signed_u16(values["vy"])
    values["scroll_x"] = _signed_u16(values["scroll_x"])
    values["scroll_y"] = _signed_u16(values["scroll_y"])
    values["horizontal_response"] = _signed_u16(values["horizontal_response"])
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
        for name in (
            "x",
            "y",
            "world_x",
            "world_y",
            "vx",
            "vy",
            "frame_ptr",
            "facing_x_flip",
            "animation_pc",
            "animation_timer",
        ):
            player[name] = sync[name]
        # Lua's canonical state schema treats TERRAIN_LANDING_STATE == 1 as
        # grounded.  0xFF is the active response latch during the jump
        # transition, not the externally reported grounded boolean.
        player["grounded"] = sync["grounded"] == 1
        player["animation_selector"] = {
            name: sync[name]
            for name in ANIMATION_SELECTOR_FIELDS
        }

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


MAME_RUN_ENVIRONMENT = (
    "OPENALADDIN_TRACE_DIR",
    "OPENALADDIN_TRACE_FRAMES",
    "OPENALADDIN_INPUT",
    "OPENALADDIN_INPUT_MODE",
    "OPENALADDIN_INPUT_OUTPUT",
    "OPENALADDIN_EVENT_OUTPUT",
    "OPENALADDIN_EVENT_SPEC",
    "OPENALADDIN_STATE_OUTPUT",
    "OPENALADDIN_CAPTURE",
    "OPENALADDIN_CAPTURE_VDP",
    "OPENALADDIN_TRACE_ACTORS",
    "OPENALADDIN_TRACE_ACTOR_INIT",
    "OPENALADDIN_TRACE_RNC_LOADS",
    "OPENALADDIN_STATE_SYNC",
    "OPENALADDIN_TRACE_EDGES",
    "OPENALADDIN_TRACE_AUDIO",
    "OPENALADDIN_TRACE_AUDIO_MAILBOX",
    "OPENALADDIN_TRACE_AUDIO_MAILBOX_READS",
    "OPENALADDIN_AUDIO_MAILBOX_READ_FRAMES",
    "OPENALADDIN_TRACE_AUDIO_COMMANDS",
    "OPENALADDIN_DEBUG_WATCH",
    "OPENALADDIN_BREAKPOINTS",
    "OPENALADDIN_LOAD_STATE",
    "OPENALADDIN_PRELOAD_STATE",
    "OPENALADDIN_PRELOAD_BEFORE_CAPTURE",
    "OPENALADDIN_CHECKPOINTS",
    "OPENALADDIN_CHECKPOINT_REFERENCE",
    "OPENALADDIN_STATE_DIRECTORY",
    "OPENALADDIN_INPUT_DIRECTORY",
    "OPENALADDIN_RECORD_FILE",
    "OPENALADDIN_PLAYBACK_FILE",
    "OPENALADDIN_MAME_HEADLESS",
    "OPENALADDIN_MAME_VIDEO",
    "OPENALADDIN_MAME_DEBUG_UI",
)


def _clean_mame_environment() -> dict[str, str]:
    environment = os.environ.copy()
    for key in MAME_RUN_ENVIRONMENT:
        environment.pop(key, None)
    return environment


def _finish_record_manifest(
    run_dir: Path,
    manifest: dict[str, Any],
    status: int,
) -> tuple[int, bool]:
    input_path = run_dir / "input.jsonl"
    valid = True
    frames = 0
    if input_path.is_file():
        try:
            _, records = load_input_timeline(input_path)
            frames = len(records)
        except SystemExit as error:
            print(f"record: invalid input timeline: {error}", file=sys.stderr)
            valid = False
    else:
        valid = False
        print(f"record: missing {input_path}", file=sys.stderr)
    event_count = 0
    segment_count = 0
    if valid:
        try:
            event_count, segment_count = _write_segments(run_dir, frames)
        except SystemExit as error:
            print(f"record: invalid event timeline: {error}", file=sys.stderr)
            valid = False
    manifest["events"] = event_count
    manifest["segments"] = segment_count
    manifest["frames"] = frames
    manifest["status"] = "complete" if status == 0 and valid and frames > 0 else "failed"
    manifest["ended_at"] = datetime.now(timezone.utc).isoformat()
    _write_json(run_dir / "run.json", manifest)
    return (0 if status == 0 and valid and frames > 0 else (status or 1), valid)


def command_record(args: argparse.Namespace) -> int:
    rom = resolve(args.rom)
    if not rom.is_file():
        raise SystemExit(f"ROM not found: {rom}")
    run_dir = run_directory(args.name)
    if run_dir.exists():
        raise SystemExit(f"run directory already exists: {run_dir}")
    if args.frames is not None and args.frames < 1:
        raise SystemExit("--frames must be positive when supplied")

    run_dir.mkdir(parents=True)
    (run_dir / "checkpoints").mkdir()
    manifest = _new_run_manifest(args, run_dir, rom)
    _write_json(run_dir / "run.json", manifest)

    environment = _clean_mame_environment()
    frame_limit = -1 if args.frames is None else args.frames - 1
    environment.update({
        "OPENALADDIN_TRACE_DIR": str(run_dir),
        "OPENALADDIN_TRACE_FRAMES": str(frame_limit),
        "OPENALADDIN_CAPTURE": "state",
        "OPENALADDIN_INPUT_MODE": "record",
        "OPENALADDIN_INPUT_OUTPUT": str(run_dir / "input.jsonl"),
        "OPENALADDIN_EVENT_OUTPUT": str(run_dir / "events.jsonl"),
        "OPENALADDIN_EVENT_SPEC": event_detector_protocol() or "",
        "OPENALADDIN_RECORD_FILE": str(run_dir / "mame.inp"),
        "OPENALADDIN_STATE_DIRECTORY": str(run_dir / "checkpoints"),
        "OPENALADDIN_CHECKPOINT_REFERENCE": "checkpoints",
        "OPENALADDIN_MAME_HEADLESS": "0",
        "OPENALADDIN_MAME_VIDEO": "soft",
        "OPENALADDIN_ROM_SHA256": manifest["rom_sha256"],
    })
    if args.load_state:
        environment["OPENALADDIN_LOAD_STATE"] = str(resolve(Path(args.load_state)))
    if args.checkpoints:
        environment["OPENALADDIN_CHECKPOINTS"] = args.checkpoints

    print(f"record: interactive MAME session for {args.name}")
    print(f"record: quit MAME when the run is complete; output will be {run_dir}")
    status = run_shell_tool("openaladdin/mame/run.sh", [str(rom)], env=environment)
    final_status, valid = _finish_record_manifest(run_dir, manifest, status)
    if valid:
        print(f"record: {run_dir}")
        print(f"record: frames {manifest['frames']}")
    return final_status


def command_mame(args: argparse.Namespace) -> int:
    """Launch a general interactive MAME session through the project wrapper."""
    rom = resolve(args.rom)
    if not rom.is_file():
        raise SystemExit(f"ROM not found: {rom}")
    if args.frames is not None and args.frames < 1:
        raise SystemExit("--frames must be positive when supplied")
    if args.mame_record and args.mame_playback:
        raise SystemExit("--mame-record and --mame-playback are mutually exclusive")
    if args.input and args.mame_playback:
        raise SystemExit("--input cannot be combined with --mame-playback")

    trace_dir = resolve(args.trace_dir) if args.trace_dir else ROOT / "build/re/mame-session"
    environment = _clean_mame_environment()
    environment.update({
        "OPENALADDIN_TRACE_DIR": str(trace_dir),
        # The interactive launcher should not stop after the wrapper's normal
        # 120-frame trace default. A supplied count means that many captured
        # logical frames, including frame zero.
        "OPENALADDIN_TRACE_FRAMES": (
            "-1" if args.frames is None else str(args.frames - 1)
        ),
        "OPENALADDIN_CAPTURE": args.capture,
        "OPENALADDIN_STATE_OUTPUT": "1",
        "OPENALADDIN_EVENT_OUTPUT": str(trace_dir / "events.jsonl"),
        "OPENALADDIN_EVENT_SPEC": event_detector_protocol() or "",
        "OPENALADDIN_MAME_HEADLESS": "1" if args.headless else "0",
        "OPENALADDIN_MAME_VIDEO": args.video,
        "OPENALADDIN_MAME_DEBUG_UI": "1" if args.debug_ui else "0",
        "OPENALADDIN_INPUT_MODE": "inject" if args.input else "record",
        "OPENALADDIN_ROM_SHA256": hashes(rom)["sha256"],
    })
    if args.input:
        environment["OPENALADDIN_INPUT"] = args.input
    if args.load_state:
        environment["OPENALADDIN_LOAD_STATE"] = str(resolve(Path(args.load_state)))
    if args.checkpoints:
        environment["OPENALADDIN_CHECKPOINTS"] = args.checkpoints
    if args.mame_record:
        environment["OPENALADDIN_RECORD_FILE"] = str(resolve(Path(args.mame_record)))
    if args.mame_playback:
        environment["OPENALADDIN_PLAYBACK_FILE"] = str(resolve(Path(args.mame_playback)))

    print(f"mame: launching {rom}")
    print(f"mame: trace output {trace_dir}")
    return run_shell_tool("openaladdin/mame/run.sh", [str(rom)], env=environment)


def _load_run_manifest(name: str) -> tuple[Path, dict[str, Any]]:
    run_dir = run_directory(name)
    manifest_path = run_dir / "run.json"
    if not manifest_path.is_file():
        raise SystemExit(f"run manifest not found: {manifest_path}")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise SystemExit(f"{manifest_path}: invalid JSON: {error}") from error
    if manifest.get("format") != RUN_FORMAT:
        raise SystemExit(f"{manifest_path}: unsupported run format {manifest.get('format')!r}")
    return run_dir, manifest


def _run_rom(args: argparse.Namespace, manifest: dict[str, Any]) -> Path:
    if args.rom is not None:
        rom = resolve(args.rom)
    else:
        rom_path = Path(str(manifest.get("rom_path", "")))
        rom = resolve(rom_path) if not rom_path.is_absolute() else rom_path
    if not rom.is_file():
        raise SystemExit(f"ROM not found: {rom}")
    expected = manifest.get("rom_sha256")
    actual = hashes(rom)["sha256"]
    if expected and actual != expected:
        raise SystemExit(f"ROM mismatch for run: expected {expected}, got {actual}")
    return rom


def _update_replay_manifest(
    run_dir: Path,
    manifest: dict[str, Any],
    client: str,
    trace: Path,
    status: int,
    segment_id: str | None = None,
) -> None:
    replays = manifest.setdefault("replays", {})
    replay_key = client if segment_id is None else f"{client}:{segment_id}"
    replay = {
        "status": "complete" if status == 0 else "failed",
        "trace": _relative_to_root(trace),
        "updated_at": datetime.now(timezone.utc).isoformat(),
    }
    if segment_id is not None:
        replay["segment"] = segment_id
    replays[replay_key] = replay
    _write_json(run_dir / "run.json", manifest)


def _apply_run_start(environment: dict[str, str], manifest: dict[str, Any]) -> None:
    start = manifest.get("start") or {}
    if start.get("type") != "save_state":
        return
    state_path = Path(str(start.get("path", "")))
    state = resolve(state_path) if not state_path.is_absolute() else state_path
    if not state.is_file():
        raise SystemExit(f"recorded start state not found: {state}")
    expected = start.get("sha256")
    if expected:
        actual = hashes(state)["sha256"]
        if actual != expected:
            raise SystemExit(
                f"recorded start state mismatch: expected {expected}, got {actual}"
            )
    environment["OPENALADDIN_LOAD_STATE"] = str(state)


def _prepare_segment_replay(
    run_dir: Path,
    segment: dict[str, Any],
    input_header: dict[str, Any] | None,
    records: list[dict[str, Any]],
    replay_dir: Path,
    *,
    client: str = "mame",
) -> tuple[Path, dict[str, Any], list[str], int]:
    if client == "native":
        native_start_frame = segment.get("native_start_frame")
        if native_start_frame is None:
            readiness = segment.get("native_ready") or {}
            reason = readiness.get("reason", "no stable native boundary was found")
            raise SystemExit(
                f"segment {segment['id']!r} has no native parity boundary: {reason}"
            )
        start_frame = int(native_start_frame)
    else:
        start_frame = int(segment["start_frame"])
    end_frame = int(segment["end_frame"])
    segment_id = str(segment["id"])
    if end_frame >= len(records):
        raise SystemExit(
            f"segment {segment_id!r} ends at frame {end_frame}, "
            f"but the input timeline has {len(records)} frame(s)"
        )
    reference = replay_dir / "genesis.jsonl"
    initial_state = _write_sliced_state(
        run_dir / "state.jsonl",
        reference,
        start_frame,
        end_frame,
        segment_id,
    )
    _write_sliced_input(
        replay_dir / "input.jsonl",
        input_header,
        records,
        start_frame,
        end_frame,
        segment_id,
    )
    # The endpoint state is captured after the final update. The native
    # client consumes start..end-1; MAME also receives the endpoint token for
    # its final boundary sample, which preserves the recorded state label.
    tokens = input_tokens(records[start_frame:end_frame + 1])
    return reference, initial_state, tokens, end_frame - start_frame


def _mame_segment_input_tokens(tokens: list[str]) -> list[str]:
    """Return tokens aligned with the first frame after a loaded MAME state.

    ``OPENALADDIN_PRELOAD_BEFORE_CAPTURE`` loads the checkpoint before the
    initial capture, but the first emulated frame uses the controls applied by
    that initial capture.  The initial sample is discarded when the replay
    trace is rebased, so the first canonical segment token must be installed
    there; inserting a neutral bootstrap shifts every input edge by one frame.
    """
    return list(tokens)


def command_replay(args: argparse.Namespace) -> int:
    run_dir, manifest = _load_run_manifest(args.name)
    rom = _run_rom(args, manifest)
    input_header, records = load_input_timeline(run_dir / "input.jsonl")
    frame_count = len(records)
    segment = select_segment(run_dir, args.segment) if args.segment else None
    segment_id = str(segment["id"]) if segment else None
    replay_dir = run_dir / "replay" / args.client
    if segment:
        replay_dir /= str(segment["slug"])
    replay_dir.mkdir(parents=True, exist_ok=True)

    if args.client == "mame":
        if segment:
            reference, initial_state, tokens, segment_frames = _prepare_segment_replay(
                run_dir, segment, input_header, records, replay_dir, client="mame"
            )
            mame_state_reference = str(segment.get("mame_state", ""))
            if not mame_state_reference:
                raise SystemExit(f"segment {segment_id!r} has no MAME save-state reference")
            mame_state = _event_state_path(run_dir, mame_state_reference)
            if not mame_state.is_file():
                raise SystemExit(f"segment {segment_id!r} MAME state not found: {mame_state}")
            expected_state_hash = segment.get("mame_state_sha256")
            if expected_state_hash and hashes(mame_state)["sha256"] != expected_state_hash:
                raise SystemExit(f"segment {segment_id!r} MAME state hash mismatch: {mame_state}")
            trace = replay_dir / "state.jsonl"
            # The preloaded state is captured once before emulation, leaving
            # one checkpoint sample at frame zero. The first emulated frame
            # must already consume the segment's first canonical token; the
            # checkpoint sample is removed by rebasing after MAME exits.
            mame_tokens = _mame_segment_input_tokens(
                _client_input_tokens(input_header, tokens)
            )
            environment = _clean_mame_environment()
            environment.update({
                "OPENALADDIN_TRACE_DIR": str(replay_dir),
                "OPENALADDIN_TRACE_FRAMES": str(segment_frames + 1),
                "OPENALADDIN_CAPTURE": "state",
                "OPENALADDIN_INPUT_MODE": "inject",
                "OPENALADDIN_INPUT": compress_input_schedule(mame_tokens),
                "OPENALADDIN_INPUT_OUTPUT": str(replay_dir / "mame-input.jsonl"),
                "OPENALADDIN_STATE_DIRECTORY": str(replay_dir / "checkpoints"),
                "OPENALADDIN_CHECKPOINT_REFERENCE": "checkpoints",
                "OPENALADDIN_LOAD_STATE": str(mame_state),
                "OPENALADDIN_PRELOAD_BEFORE_CAPTURE": "1",
                "OPENALADDIN_MAME_HEADLESS": "1",
                "OPENALADDIN_MAME_VIDEO": "none",
                "OPENALADDIN_ROM_SHA256": manifest["rom_sha256"],
            })
            if initial_state.get("actors"):
                environment["OPENALADDIN_TRACE_ACTORS"] = "1"
            status = run_shell_tool("openaladdin/mame/run.sh", [str(rom)], env=environment)
            if status == 0:
                if not reference.is_file() or not trace.is_file():
                    print("replay: segment reference or replay state trace is missing", file=sys.stderr)
                    status = 1
                else:
                    _rebase_state_trace_in_place(
                        trace,
                        1,
                        segment_frames + 1,
                        segment_id or "segment",
                        input_tokens=tokens,
                    )
                    status = run_tool(
                        "openaladdin/mame/compare_state.py",
                        [str(reference), str(trace)],
                    )
            _update_replay_manifest(run_dir, manifest, args.client, trace, status, segment_id)
            if status == 0:
                print(f"replay: MAME segment {segment_id} PASS ({segment_frames + 1} state frame(s))")
            else:
                print(f"replay: MAME segment {segment_id} failed; trace {trace}", file=sys.stderr)
            return status

        mame_input = run_dir / "mame.inp"
        if not mame_input.is_file():
            raise SystemExit(f"MAME input recording not found: {mame_input}")
        trace = replay_dir / "state.jsonl"
        environment = _clean_mame_environment()
        environment.update({
            "OPENALADDIN_TRACE_DIR": str(replay_dir),
            "OPENALADDIN_TRACE_FRAMES": str(max(frame_count - 1, 0)),
            "OPENALADDIN_CAPTURE": "state",
            "OPENALADDIN_INPUT_MODE": "playback",
            "OPENALADDIN_INPUT_OUTPUT": str(replay_dir / "input.jsonl"),
            "OPENALADDIN_PLAYBACK_FILE": str(mame_input),
            "OPENALADDIN_STATE_DIRECTORY": str(replay_dir / "checkpoints"),
            "OPENALADDIN_CHECKPOINT_REFERENCE": "checkpoints",
            "OPENALADDIN_MAME_HEADLESS": "1",
            "OPENALADDIN_MAME_VIDEO": "none",
            "OPENALADDIN_ROM_SHA256": manifest["rom_sha256"],
        })
        _apply_run_start(environment, manifest)
        status = run_shell_tool("openaladdin/mame/run.sh", [str(rom)], env=environment)
        if status == 0:
            original = run_dir / "state.jsonl"
            if not original.is_file() or not trace.is_file():
                print("replay: original or replay state trace is missing", file=sys.stderr)
                status = 1
            else:
                if input_header and input_header.get("controller_mapping") is None:
                    _relabel_state_trace_inputs(
                        trace,
                        input_tokens(records),
                        "legacy-full-replay",
                    )
                status = run_tool(
                    "openaladdin/mame/compare_state.py",
                    [str(original), str(trace)],
                )
        _update_replay_manifest(run_dir, manifest, args.client, trace, status)
        if status == 0:
            print(f"replay: MAME PASS ({frame_count} frame(s))")
        else:
            print(f"replay: MAME failed; trace {trace}", file=sys.stderr)
        return status

    trace = replay_dir / "state.jsonl"
    if segment:
        reference, initial_state, tokens, segment_frames = _prepare_segment_replay(
            run_dir, segment, input_header, records, replay_dir, client="native"
        )
        native_frames = segment_frames
        scheduled_tokens = (
            # Native writes the checkpoint as state frame 0, then applies one
            # scheduled token before writing each subsequent state. The
            # checkpoint token is therefore already represented by frame 0;
            # consume source frame start+1 on the first native update.
            _client_input_tokens(input_header, tokens[1:]) if tokens else []
        )
    else:
        reference = run_dir / "state.jsonl"
        initial_state = None
        native_frames = max(frame_count - 1, 0)
        scheduled_tokens = _client_input_tokens(input_header, input_tokens(records))
    native_command = [
        str(ROOT / "run.sh"),
        "--no-window",
        "--no-audio",
        "--rom", str(rom),
        "--frames", str(native_frames),
        "--state-output", str(trace),
        "--input-schedule", readable_input_schedule(scheduled_tokens),
    ]
    if segment:
        phase = segment.get("native_animation_phase") or {}
        phase_delay = (
            int(phase["delay_ticks"])
            if phase.get("status") == "inferred" else None
        )
        native_command.extend(native_checkpoint_arguments(
            initial_state or {},
            animation_phase_delay=phase_delay,
        ))
    native_environment = os.environ.copy()
    native_environment["SDL_VIDEODRIVER"] = "dummy"
    status = subprocess.run(native_command, cwd=ROOT, env=native_environment, check=False).returncode
    _update_replay_manifest(run_dir, manifest, args.client, trace, status, segment_id)
    if status == 0:
        if input_header and input_header.get("controller_mapping") is None:
            source_tokens = input_tokens(records)
            if segment:
                source_tokens = source_tokens[int(segment["native_start_frame"]):]
            _relabel_state_trace_inputs(
                trace,
                source_tokens,
                "legacy-native-replay",
            )
        if segment:
            print(f"replay: native segment {segment_id} trace {trace}")
        else:
            print(f"replay: native trace {trace}")
    return status


def command_parity(args: argparse.Namespace) -> int:
    run_dir, _ = _load_run_manifest(args.name)
    segment = select_segment(run_dir, args.segment) if args.segment else None
    if segment:
        segment_dir = run_dir / "replay" / "native" / str(segment["slug"])
        genesis = segment_dir / "genesis.jsonl"
        native = segment_dir / "state.jsonl"
    else:
        genesis = run_dir / "state.jsonl"
        native = run_dir / "replay" / "native" / "state.jsonl"
    if not genesis.is_file():
        raise SystemExit(f"recorded state trace not found: {genesis}")
    if not native.is_file():
        suffix = f" --segment {args.segment}" if args.segment else ""
        raise SystemExit(f"native replay not found: {native}; run replay {args.name} --client native{suffix}")
    fields = args.fields or DEFAULT_PARITY_FIELDS
    forwarded = [str(genesis), str(native)]
    for field in fields:
        forwarded.extend(["--field", field])
    return run_tool("openaladdin/mame/compare_state.py", forwarded)


def command_inputs_summarize(args: argparse.Namespace) -> int:
    path = resolve(args.input)
    _, records = load_input_timeline(path)
    print(readable_input_schedule(input_tokens(records)))
    return 0


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
        "player.facing_x_flip",
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
        "OPENALADDIN_POKE_FRAME",
        "OPENALADDIN_POKE_MEMORY",
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
    if any(field == "actors" or field.startswith("actors[") for field in fields):
        environment["OPENALADDIN_TRACE_ACTORS"] = "1"
    if state_sync:
        environment["OPENALADDIN_STATE_SYNC"] = "1"
    protocol = experiment_action_protocol(experiment)
    if protocol:
        environment["OPENALADDIN_EXPERIMENT_ACTIONS"] = protocol
    memory_pokes = experiment_memory_pokes(experiment)
    if memory_pokes:
        environment["OPENALADDIN_POKE_FRAME"] = str(memory_pokes[0])
        environment["OPENALADDIN_POKE_MEMORY"] = memory_pokes[1]

    print(f"regression: running MAME experiment {args.scenario}")
    status = run_shell_tool("openaladdin/mame/run.sh", [str(rom)], env=environment)
    if status:
        return status
    if state_sync:
        synchronize_state_trace(mame_trace)

    mame_source = mame_trace / "state.jsonl"
    compare_frames, checkpoint_frame, checkpoint = aligned_trace(mame_source, aligned, marker_name, fields)
    _, mame_states, _ = load_state_trace(mame_source)
    # MAME's frame-state record labels the input that is consumed by the
    # following stable update boundary. The native file begins with the
    # checkpoint record before its first update, so replay the token at the
    # same aligned index; this keeps a one-frame edge (such as jump launch)
    # from being applied before the state that carries its input.
    input_tokens = [
        str(mame_states[checkpoint_frame + relative].get("input", "none"))
        for relative in range(compare_frames)
    ]

    native_environment = os.environ.copy()
    native_environment["SDL_VIDEODRIVER"] = "dummy"
    native_command = [
        str(ROOT / "run.sh"),
        "--no-window",
        "--frames", str(compare_frames),
        "--state-output", str(native_trace),
        "--input-schedule", compress_input_schedule(input_tokens),
    ]
    # A recorded terrain byte is the state at the checkpoint, not a global
    # fixture. Let the native resolver follow the player's position so a
    # replay can cross into a different behavior cell after the checkpoint.
    native_command.extend(native_checkpoint_arguments(checkpoint, include_terrain_behavior=False))
    actor_records = regression.get("actor_records")
    if actor_records:
        native_command.extend(["--actor-records", str(resolve(Path(actor_records)))])
    print(f"regression: checkpoint {marker_name} at MAME frame {checkpoint_frame}")
    print(f"regression: replaying {compare_frames} post-checkpoint frame(s) natively")
    status = subprocess.run(native_command, cwd=ROOT, env=native_environment, check=False).returncode
    if status:
        return status

    actor_table_compare = "actors" in fields
    state_fields = [field for field in fields if field != "actors"]
    print(f"regression: comparing fields {', '.join(fields)}")
    actor_status = 0
    if actor_table_compare:
        actor_args: list[str] = [str(aligned), str(native_trace)]
        for field in regression.get("actor_fields") or []:
            actor_args.extend(["--field", str(field)])
        actor_status = run_tool("openaladdin/mame/compare_actors.py", actor_args)

    state_status = 0
    if state_fields:
        compare_args: list[str] = [str(aligned), str(native_trace)]
        for field in state_fields:
            compare_args.extend(["--field", field])
        state_status = run_tool("openaladdin/mame/compare_state.py", compare_args)
    status = actor_status or state_status
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

    record = commands.add_parser(
        "record",
        help="record an interactive MAME run as a canonical input/state corpus",
    )
    record.add_argument("name", help="run name stored below build/runs/")
    add_rom_argument(record)
    record.add_argument("--frames", type=int, help="optional frame count for automated/smoke recording")
    record.add_argument("--load-state", help="start from a MAME save-state file")
    record.add_argument(
        "--checkpoints",
        help="named MAME save states as frame=name pairs, e.g. 0=boot,1245=level01-entry",
    )
    record.add_argument("--controller", default="P1 Mega Drive pad")
    record.set_defaults(function=command_record)

    mame = commands.add_parser(
        "mame",
        help="launch an interactive MAME session through the project wrapper",
    )
    add_rom_argument(mame)
    mame.add_argument("--frames", type=int, help="optional frame count; otherwise run until MAME exits")
    mame.add_argument("--trace-dir", type=Path)
    mame.add_argument("--capture", choices=("state", "ram", "vdp", "full"), default="state")
    mame.add_argument("--input", help="optional deterministic input schedule; disables passive input observation")
    mame.add_argument("--load-state")
    mame.add_argument(
        "--checkpoints",
        help="named MAME save states as frame=name pairs, e.g. 0=boot,1245=level01-entry",
    )
    mame.add_argument("--headless", action="store_true", help="run without a visible MAME window")
    mame.add_argument("--video", default="soft", help="MAME video backend (default: soft)")
    mame.add_argument("--debug-ui", action="store_true", help="show the MAME debugger UI")
    mame.add_argument("--mame-record", type=Path, help="write MAME's native input recording to this file")
    mame.add_argument("--mame-playback", type=Path, help="play MAME's native input recording from this file")
    mame.set_defaults(function=command_mame)

    replay = commands.add_parser(
        "replay",
        help="replay a recorded run with MAME's native input or OpenAladdin",
    )
    replay.add_argument("name")
    replay.add_argument("--client", choices=("mame", "native"), default="mame")
    replay.add_argument("--rom", type=Path)
    replay.add_argument("--segment", help="replay only a detected segment, starting from its checkpoint")
    replay.set_defaults(function=command_replay)

    parity = commands.add_parser(
        "parity",
        help="compare a recorded MAME state trace with its native replay",
    )
    parity.add_argument("name")
    parity.add_argument("--field", dest="fields", action="append")
    parity.add_argument("--segment", help="compare only a detected segment")
    parity.set_defaults(function=command_parity)

    inputs = commands.add_parser("inputs", help="inspect canonical input timelines")
    input_commands = inputs.add_subparsers(dest="inputs_command", required=True)
    summarize = input_commands.add_parser("summarize", help="render JSONL input as an RLE schedule")
    summarize.add_argument("input", type=Path)
    summarize.set_defaults(function=command_inputs_summarize)

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
