#!/usr/bin/env python3
"""Compare versioned per-frame state JSONL files.

The comparator deliberately reports the first differing leaf rather than a
whole-record diff. This keeps a physics mismatch actionable when the state
record contains many unrelated actor fields.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


FORMATS = {
    "openaladdin-frame-state-v1",
    "openaladdin-frame-state-v2",
    "openaladdin-frame-state-v3",
    "openaladdin-core-trace-v1",
}


def load_states(path: Path) -> tuple[dict[str, Any] | None, dict[int, dict[str, Any]]]:
    header = None
    states: dict[int, dict[str, Any]] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise SystemExit(f"{path}:{line_number}: invalid JSON: {error}") from error
        if record.get("type") == "header":
            header = record
            continue
        if record.get("type") not in (None, "state", "frame_state"):
            continue
        if "frame" not in record:
            raise SystemExit(f"{path}:{line_number}: state record has no frame")
        states[int(record["frame"])] = record
    if header and header.get("format") not in (None, *FORMATS):
        raise SystemExit(f"{path}: unsupported state format {header.get('format')!r}")
    if not states:
        raise SystemExit(f"{path}: no frame state records")
    return header, states


def atomic_frames(
    header: dict[str, Any] | None,
    states: dict[int, dict[str, Any]],
) -> set[int]:
    """Return frames explicitly captured as one atomic game-loop state."""
    if not header:
        return set()
    sync = header.get("sync") or {}
    fields = set(sync.get("atomic_fields") or [])
    if "actors" not in fields:
        return set()
    return {
        frame
        for frame, record in states.items()
        if isinstance(record.get("capture"), dict)
        and record["capture"].get("atomic") is True
    }


def require_atomic_trace(
    path: Path,
    header: dict[str, Any] | None,
    states: dict[int, dict[str, Any]],
    *,
    label: str,
) -> set[int]:
    sync = header.get("sync") if header else None
    if not isinstance(sync, dict) or not sync.get("actors_qualified"):
        raise SystemExit(
            f"{path}: {label} trace is not actor-qualified; "
            "requires an actor-qualified atomic game-loop capture"
        )
    frames = atomic_frames(header, states)
    if not frames:
        raise SystemExit(f"{path}: {label} trace has no atomic game-loop state frames")
    return frames


def first_difference(
    left: Any,
    right: Any,
    path: str = "",
    *,
    allow_additional_right_fields: bool = False,
) -> tuple[str, Any, Any] | None:
    if type(left) is not type(right):
        return path or "$", left, right
    if isinstance(left, dict):
        keys = sorted(set(left) | set(right))
        for key in keys:
            child = f"{path}.{key}" if path else str(key)
            if key not in left or key not in right:
                if allow_additional_right_fields and key not in left:
                    continue
                return child, left.get(key), right.get(key)
            difference = first_difference(
                left[key],
                right[key],
                child,
                allow_additional_right_fields=allow_additional_right_fields,
            )
            if difference:
                return difference
        return None
    if isinstance(left, list):
        for index in range(max(len(left), len(right))):
            child = f"{path}[{index}]"
            if index >= len(left) or index >= len(right):
                return child, left[index] if index < len(left) else None, right[index] if index < len(right) else None
            difference = first_difference(
                left[index],
                right[index],
                child,
                allow_additional_right_fields=allow_additional_right_fields,
            )
            if difference:
                return difference
        return None
    return None if left == right else (path or "$", left, right)


def field_value(record: dict[str, Any], field: str) -> Any:
    value: Any = record
    for component in field.split("."):
        match = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)(?:\[(\d+)\])?", component)
        if match is None:
            raise KeyError(field)
        name, index = match.groups()
        if not isinstance(value, dict) or name not in value:
            raise KeyError(field)
        value = value[name]
        if index is not None:
            if not isinstance(value, list):
                raise KeyError(field)
            slot = int(index)
            matches = [item for item in value if isinstance(item, dict) and item.get("slot") == slot]
            if not matches:
                raise KeyError(field)
            value = matches[0]
    return value


def inactive_actor_slot(record: dict[str, Any], slot: int) -> bool:
    for actor in record.get("actors", []):
        if isinstance(actor, dict) and actor.get("slot") == slot:
            return int(actor.get("type", 0)) == 0 and int(actor.get("flags", 0)) == 0
    return True


def selected_difference(left: dict[str, Any], right: dict[str, Any], fields: list[str]) -> tuple[str, Any, Any] | None:
    for field in fields:
        actor_match = re.fullmatch(r"actors\[(\d+)\]\..+", field)
        if actor_match is not None:
            slot = int(actor_match.group(1))
            # MAME's actor capture omits inactive slots, while the native
            # state schema emits the fixed-width table. Compare selected
            # fields only while at least one side has a live actor.
            if inactive_actor_slot(left, slot) and inactive_actor_slot(right, slot):
                continue
        try:
            left_value = field_value(left, field)
        except KeyError:
            left_value = None
        try:
            right_value = field_value(right, field)
        except KeyError:
            right_value = None
        difference = first_difference(left_value, right_value, field)
        if difference:
            return difference
    return None


def changed_leaves(
    before: Any,
    after: Any,
    path: str = "",
    *,
    limit: int = 16,
) -> list[tuple[str, Any, Any]]:
    """Return a compact causal delta between two matching-boundary states."""
    ignored = {"type", "format", "frame", "input", "capture", "causal"}
    if limit <= 0:
        return []
    if type(before) is not type(after):
        return [(path or "$", before, after)]
    if isinstance(before, dict):
        result: list[tuple[str, Any, Any]] = []
        for key in sorted(set(before) | set(after)):
            if key in ignored:
                continue
            child = f"{path}.{key}" if path else key
            if key not in before or key not in after:
                result.append((child, before.get(key), after.get(key)))
            else:
                result.extend(changed_leaves(before[key], after[key], child, limit=limit - len(result)))
            if len(result) >= limit:
                return result[:limit]
        return result
    if isinstance(before, list):
        result = []
        for index in range(max(len(before), len(after))):
            child = f"{path}[{index}]"
            if index >= len(before) or index >= len(after):
                result.append((child, before[index] if index < len(before) else None,
                               after[index] if index < len(after) else None))
            else:
                result.extend(changed_leaves(before[index], after[index], child, limit=limit - len(result)))
            if len(result) >= limit:
                return result[:limit]
        return result
    return [] if before == after else [(path or "$", before, after)]


def print_state_changes(
    label: str,
    previous: dict[str, Any],
    current: dict[str, Any],
) -> None:
    changes = changed_leaves(previous, current)
    print(f"{label} changes since S[{previous.get('frame', '?')}]:")
    if not changes:
        print("  (none)")
        return
    for path, before, after in changes:
        print(f"  {path}: {json.dumps(before, sort_keys=True)} -> {json.dumps(after, sort_keys=True)}")


def print_writer_pcs(label: str, record: dict[str, Any]) -> None:
    causal = record.get("causal")
    if not isinstance(causal, dict):
        return
    writers = causal.get("writer_pcs") or causal.get("writer_pcs_observed")
    if writers:
        print(f"{label} writer PCs observed: {json.dumps(writers, sort_keys=True)}")


def print_divergence_context(
    frame: int,
    left: dict[int, dict[str, Any]],
    right: dict[int, dict[str, Any]],
    frames: list[int],
) -> None:
    left_record = left.get(frame, {})
    right_record = right.get(frame, {})
    previous = [candidate for candidate in frames if candidate < frame and candidate in left and candidate in right]
    previous_frame = previous[-1] if previous else None
    print(f"Last matching state: S[{previous_frame if previous_frame is not None else 'none'}]")
    input_frame = previous_frame if previous_frame is not None else frame
    genesis_input = left.get(input_frame, {}).get("input", "none")
    openaladdin_input = right.get(input_frame, {}).get("input", "none")
    print(f"Input: I[{input_frame}] Genesis={genesis_input} OpenAladdin={openaladdin_input}")
    if previous_frame is not None:
        print_state_changes("Genesis", left[previous_frame], left_record)
        print_state_changes("OpenAladdin", right[previous_frame], right_record)
    print_writer_pcs("Genesis", left_record)
    print_writer_pcs("OpenAladdin", right_record)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("genesis", type=Path)
    parser.add_argument("openaladdin", type=Path)
    parser.add_argument(
        "--field",
        action="append",
        dest="fields",
        help="compare only this dotted state field; repeat for multiple fields",
    )
    parser.add_argument(
        "--allow-additional-fields",
        action="store_true",
        help=(
            "allow fields present only in the right-hand trace; use when "
            "replaying an older reference with a newer recorder schema"
        ),
    )
    parser.add_argument(
        "--require-left-atomic",
        action="store_true",
        help="require the left/reference trace to contain actor-qualified atomic states",
    )
    parser.add_argument(
        "--require-atomic",
        action="store_true",
        help="require both traces to contain actor-qualified atomic states",
    )
    parser.add_argument(
        "--atomic-only",
        action="store_true",
        help="compare only frames marked atomic in both traces",
    )
    parser.add_argument(
        "--left-atomic-only",
        action="store_true",
        help="compare only frames marked atomic in the left/reference trace",
    )
    parser.add_argument(
        "--right-frame-offset",
        type=int,
        default=0,
        help="compare left frame N with right frame N plus this offset",
    )
    args = parser.parse_args()
    left_header, left = load_states(args.genesis.resolve())
    right_header, right = load_states(args.openaladdin.resolve())
    if left_header and right_header:
        left_rom = left_header.get("rom_sha256")
        right_rom = right_header.get("rom_sha256")
        if left_rom and right_rom and left_rom != right_rom:
            raise SystemExit(f"ROM mismatch: Genesis={left_rom} OpenAladdin={right_rom}")

    left_atomic = set()
    right_atomic = set()
    if args.require_left_atomic or args.atomic_only:
        left_atomic = require_atomic_trace(
            args.genesis.resolve(), left_header, left, label="left/reference"
        )
    if args.require_atomic or args.atomic_only:
        right_atomic = require_atomic_trace(
            args.openaladdin.resolve(), right_header, right, label="right/replay"
        )
    right_frame_offset = args.right_frame_offset
    right_view = (
        dict(right)
        if right_frame_offset == 0
        else {
            frame - right_frame_offset: record
            for frame, record in right.items()
            if frame - right_frame_offset in left
        }
    )
    right_atomic_view = {
        frame - right_frame_offset
        for frame in right_atomic
    }
    if args.atomic_only:
        if left_atomic != right_atomic_view:
            missing_left = sorted(right_atomic_view - left_atomic)
            missing_right = sorted(left_atomic - right_atomic_view)
            raise SystemExit(
                "atomic frame sets differ: "
                f"missing from left={missing_left[:5]} "
                f"missing from right={missing_right[:5]}"
            )
        frames = sorted(left_atomic)
    elif args.left_atomic_only:
        if not args.require_left_atomic:
            left_atomic = require_atomic_trace(
                args.genesis.resolve(), left_header, left, label="left/reference"
            )
        frames = sorted(left_atomic)
    else:
        frames = sorted(set(left) | set(right_view))
    for frame in frames:
        if frame not in left or frame not in right_view:
            print(f"First divergence: frame {frame}")
            print("state record")
            print(f"  Genesis:      {'present' if frame in left else 'missing'}")
            print(f"  OpenAladdin:  {'present' if frame in right_view else 'missing'}")
            print_divergence_context(frame, left, right_view, frames)
            return 1
        left_record = left[frame]
        right_record = right_view[frame]
        if not args.fields:
            # The header is the schema authority. Older derived records can
            # still carry the v1 per-record label after a v2 header was added;
            # it is provenance, not gameplay state.
            left_record = dict(left_record)
            right_record = dict(right_record)
            left_record.pop("format", None)
            right_record.pop("format", None)
            # Phase order and writer PCs are provenance diagnostics. They are
            # intentionally not semantic state: a native phase overlay can
            # be more explicit than the subset of ROM entry points observed
            # by a particular breakpoint run.
            left_record.pop("causal", None)
            right_record.pop("causal", None)
        difference = (
            selected_difference(left_record, right_record, args.fields)
            if args.fields
            else first_difference(
                left_record,
                right_record,
                allow_additional_right_fields=args.allow_additional_fields,
            )
        )
        if difference:
            path, genesis_value, openaladdin_value = difference
            print(f"First divergence: frame {frame}")
            print(path)
            print(f"  Genesis:      {json.dumps(genesis_value, sort_keys=True)}")
            print(f"  OpenAladdin:  {json.dumps(openaladdin_value, sort_keys=True)}")
            print_divergence_context(frame, left, right, frames)
            return 1

    print(f"Traces match for {len(frames)} frame(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
