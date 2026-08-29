"""Shared workspace, input, and run-manifest services."""

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

from genie.common import hashes, load_yaml, normalize_symbols, parse_int, rom_entries
from genie.context import ProjectContext
from genie.platforms.genesis.input import (
    INPUT_BUTTONS,
    INPUT_MASKS,
    INPUT_MAPPING,
    LEGACY_BUTTON_REMAP,
    buttons_for_mask as genesis_buttons_for_mask,
    client_input_tokens as genesis_client_input_tokens,
    token_for_mask as genesis_token_for_mask,
)
from genie.profiles import load_profile

PROJECT = ProjectContext.discover()
ROOT = PROJECT.root
SCRIPT_DIR = ROOT / "genie"
PROFILE = load_profile()
EXPERIMENTS = ROOT / PROFILE.experiments_manifest
EVENTS = ROOT / PROFILE.events_manifest
GHIDRA_CONFIG = ROOT / PROFILE.ghidra_config
ROM_DEFAULT = ROOT / PROFILE.default_rom


def _load_state_trace(path: Path):
    from genie.games.aladdin.mame.state import load_state_trace

    return load_state_trace(path)


def _animation_selector_spec(player: dict[str, Any]) -> str | None:
    from genie.games.aladdin.mame.state import animation_selector_spec

    return animation_selector_spec(player)

INPUT_FORMAT = PROFILE.input_format

INPUT_MAPPING = PROFILE.input_mapping

LEGACY_BUTTON_REMAP = {"a": "b", "b": "c", "c": "a"}

EVENT_FORMAT = PROFILE.event_format

SEGMENTS_FORMAT = PROFILE.segments_format

RUN_FORMAT = PROFILE.run_format

STATE_FORMAT_V2, STATE_FORMAT_V3 = PROFILE.state_formats

DEFAULT_PARITY_FIELDS = list(PROFILE.parity_fields)

ATOMIC_STATE_FIELDS = PROFILE.atomic_state_fields

ATOMIC_ACTOR_FIELDS = PROFILE.atomic_actor_fields

def default_rom() -> Path:
    _, expected, _ = rom_entries()
    configured = ROOT / str(expected.get("expected_filename", PROFILE.default_rom))
    if configured.is_file():
        return configured
    return ROOT / "rom/aladdin-usa.bin"

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
    return genesis_buttons_for_mask(mask)

def token_for_mask(mask: int) -> str:
    return genesis_token_for_mask(mask)

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
    return genesis_client_input_tokens(header, tokens)

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
            from genie.games.aladdin.mame.runs import _backfill_native_boundary

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

def _backfill_native_boundary(run_dir: Path, segment: dict[str, Any]) -> None:
    """Derive the new native boundary for a pre-readiness segments.json."""
    if "native_start_frame" in segment:
        return
    state_path = run_dir / "state.jsonl"
    if not state_path.is_file():
        return
    _, state_records, _ = _load_state_trace(state_path)
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
    scheduler = state.get("scheduler")
    if isinstance(scheduler, dict) and "frame_phase" in scheduler:
        descriptor["scheduler"] = {
            "frame_phase": _state_int(scheduler, "frame_phase") & 0xFF,
        }
    descriptor["player"]["grounded"] = _state_bool(
        player,
        "grounded",
        _state_int(player, "vy") == 0,
    )
    for name in ("frame_ptr", "animation_pc", "animation_timer", "facing_x_flip"):
        if name in player:
            descriptor["player"][name] = _state_int(player, name)
    from genie.games.aladdin.mame.state import ANIMATION_SELECTOR_FIELDS

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
    scheduler = checkpoint.get("scheduler") or {}
    if "frame_phase" in scheduler:
        arguments.extend([
            "--checkpoint-frame-phase",
            str(_state_int(scheduler, "frame_phase") & 0xFF),
        ])
    selector_spec = _animation_selector_spec(player)
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
    header, states, markers = _load_state_trace(source)
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
    _, states, _ = _load_state_trace(path)
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
        _, state_records, _ = _load_state_trace(state_path)
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
        "frame_contract": "S[N] = synchronized state at boundary N; I[N] = input for the transition S[N] -> S[N+1]",
        "frame_semantics": "segment frame N is state boundary S[N] and its input is transition input I[N]",
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
            "S[N] is the synchronized state at boundary N; I[N] is the controller input used for the transition S[N] -> S[N+1]"
        ),
        "artifacts": {
            "input": "input.jsonl",
            "state": "state.jsonl",
            "raw_state": "raw/state.jsonl",
            "raw_state_compat": "state.raw.jsonl",
            "synchronized_state": "state.synced.jsonl",
            "events": "events.jsonl",
            "event_source": "state.jsonl",
            "segments": "segments.json",
            "quality": "trace-quality.json",
            "capture_input": "capture-input.jsonl",
            "mame_input": "mame.inp",
            "checkpoints": "checkpoints/",
        },
        "replays": {},
    }

__all__ = [name for name in globals() if not name.startswith("__")]
