"""Asset, decoder, coverage, regression, validation, and diagnostic commands."""

from __future__ import annotations

import argparse
import os

from genie.ghidra import verify_rom
from genie.runtime import *
from genie.knowledge import *
from genie.games.aladdin.mame.experiments import *
from genie.games.aladdin.mame.state import *
from genie.games.aladdin.mame.runs import *
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
    status = run_shell_tool("mame/run.sh", [str(rom)], env=environment)
    if status:
        return status
    if state_sync:
        synchronize_state_trace(mame_trace, rom_path=rom)

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
        for relative in range(compare_frames + 1)
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
    # Native writes the token consumed to produce S[N] while the synchronized
    # MAME view labels S[N] with the transition token I[N]. Relabel the native
    # compatibility trace before comparing so divergence context reports the
    # actual input at the matching boundary rather than an artificial one-frame
    # offset.
    _relabel_state_trace_inputs(native_trace, input_tokens, "regression-input-alignment")

    actor_table_compare = "actors" in fields
    state_fields = [field for field in fields if field != "actors"]
    print(f"regression: comparing fields {', '.join(fields)}")
    actor_status = 0
    if actor_table_compare:
        actor_args: list[str] = [str(aligned), str(native_trace)]
        for field in regression.get("actor_fields") or []:
            actor_args.extend(["--field", str(field)])
        actor_status = run_tool("mame/compare_actors.py", actor_args)

    state_status = 0
    if state_fields:
        compare_args: list[str] = [str(aligned), str(native_trace)]
        for field in state_fields:
            compare_args.extend(["--field", field])
        state_status = run_tool("mame/compare_state.py", compare_args)
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

    status = run_tool(f"games/aladdin/vm/{kind}.py", forwarded)
    if status or not args.verify:
        return status
    return run_tool("games/aladdin/vm/verify.py", [kind, str(rom), str(output)])

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
    return run_tool("assets/extract.py", forwarded)

def command_coverage_merge(args: argparse.Namespace) -> int:
    forwarded: list[str] = []
    if args.trace_dirs:
        forwarded.extend(str(resolve(path)) for path in args.trace_dirs)
    else:
        forwarded.extend(["--trace-root", str(resolve(args.trace_root))])
    forwarded.extend(["--output", str(resolve(args.output))])
    return run_tool("mame/coverage.py", forwarded)

def command_coverage_import_ghidra(args: argparse.Namespace) -> int:
    forwarded = [str(resolve(args.coverage)), "--output", str(resolve(args.output))]
    if args.project_dir:
        forwarded.extend(["--project-dir", str(resolve(args.project_dir))])
    return run_tool("ghidra/import_coverage.py", forwarded)

def command_coverage_gaps(args: argparse.Namespace) -> int:
    forwarded = [
        str(resolve(args.coverage)),
        "--rom", str(resolve(args.rom)),
        "--output", str(resolve(args.output)),
    ]
    return run_tool("mame/coverage_gaps.py", forwarded)

def command_validate(args: argparse.Namespace) -> int:
    rom = resolve(args.rom)
    status = verify_rom(rom, allow_unverified=args.allow_unverified)
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
        status = run_tool("assets/validate.py", ["--assets", str(resolve(args.assets)), "--rom", str(rom)])
        if status:
            return status
    if not args.skip_scene:
        loader = ROOT / "build/assets/rnc/loader_analysis.json"
        if loader.is_file():
            status = run_tool("assets/validate_scene_resources.py")
            if status:
                return status
        else:
            print("scene resources: skipped (asset extraction is not present)")
    for kind, path in (("animation", ROOT / "build/re/animation_streams.json"), ("movement", ROOT / "build/re/movement_streams.json")):
        if path.is_file():
            status = run_tool("games/aladdin/vm/verify.py", [kind, str(rom), str(path)])
            if status:
                return status
        else:
            print(f"{kind} streams: skipped (decode output is not present)")
    return 0

def command_doctor(args: argparse.Namespace) -> int:
    """Run the shared Genie workspace diagnostics."""

    from genie.doctor import run_doctor

    return run_doctor(args)

__all__ = [name for name in globals() if not name.startswith("__")]
