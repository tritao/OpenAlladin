"""Parity, trace comparison, and input inspection commands."""

from __future__ import annotations

import argparse

from genie.runtime import *
from genie.mame.state import *
from genie.mame.runs import *
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
    genesis_header, _, _ = load_state_trace(genesis)
    if not genesis_header.get("transformations"):
        semantic = genesis.with_name(f"{genesis.stem}.semantic{genesis.suffix}")
        normalize_animation_state_trace(genesis, semantic)
        genesis = semantic
    fields = args.fields or DEFAULT_PARITY_FIELDS
    actor_fields = [
        field for field in fields
        if field == "actors" or field.startswith("actors[")
    ]
    state_fields = [field for field in fields if field not in actor_fields]
    statuses: list[int] = []
    if actor_fields:
        # Actor parity is meaningful only against the synchronized MAME
        # boundary. Native traces need not use the MAME capture metadata, so
        # restrict the comparison to the reference trace's atomic frames.
        actor_args = [
            str(genesis),
            str(native),
            "--require-left-atomic",
            "--left-atomic-only",
        ]
        statuses.append(run_tool("mame/compare_actors.py", actor_args))
    if state_fields:
        state_args = [str(genesis), str(native)]
        for field in state_fields:
            state_args.extend(["--field", field])
        statuses.append(run_tool("mame/compare_state.py", state_args))
    return next((status for status in statuses if status), 0)

def command_inputs_summarize(args: argparse.Namespace) -> int:
    path = resolve(args.input)
    _, records = load_input_timeline(path)
    print(readable_input_schedule(input_tokens(records)))
    return 0

def command_scheduler_compare(args: argparse.Namespace) -> int:
    forwarded = [str(resolve(args.genesis)), str(resolve(args.openaladdin))]
    for phase in args.phases or []:
        forwarded.extend(["--phase", phase])
    for option in ("include_pcs", "include_writers", "intersection"):
        if getattr(args, option):
            forwarded.append("--" + option.replace("_", "-"))
    if args.right_frame_offset:
        forwarded.extend(["--right-frame-offset", str(args.right_frame_offset)])
    if args.start_frame is not None:
        forwarded.extend(["--start-frame", str(args.start_frame)])
    if args.end_frame is not None:
        forwarded.extend(["--end-frame", str(args.end_frame)])
    return run_tool("mame/compare_scheduler.py", forwarded)

def command_compare(args: argparse.Namespace) -> int:
    forwarded = [str(resolve(args.genesis)), str(resolve(args.openaladdin))]
    for field in args.fields or []:
        forwarded.extend(["--field", field])
    for option in (
        "require_left_atomic",
        "require_atomic",
        "atomic_only",
        "left_atomic_only",
    ):
        if getattr(args, option):
            forwarded.append("--" + option.replace("_", "-"))
    return run_tool("mame/compare_state.py", forwarded)

__all__ = [name for name in globals() if not name.startswith("__")]
