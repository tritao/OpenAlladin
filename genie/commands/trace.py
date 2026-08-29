"""MAME trace commands."""

from __future__ import annotations

import argparse
import os

from genie.runtime import *
from genie.knowledge import validate_knowledge
from genie.games.aladdin.mame.experiments import *
from genie.games.aladdin.mame.state import *
from genie.games.aladdin.mame.runs import *
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
        "OPENALADDIN_TRACE_SCHEDULER",
        "OPENALADDIN_TRACE_SCHEDULER_CALLS",
        "OPENALADDIN_POKE_FRAME",
        "OPENALADDIN_POKE_MEMORY",
        "OPENALADDIN_CHECKPOINTS",
        "OPENALADDIN_EXECUTION_PROFILE",
        "OPENALADDIN_MAME_SOUND",
    ):
        environment.pop(key, None)
    environment.update({
        "OPENALADDIN_TRACE_DIR": str(trace_dir),
        "OPENALADDIN_TRACE_FRAMES": str(args.frames or experiment["frames"]),
        "OPENALADDIN_INPUT": str(args.input or experiment.get("input", "none")),
        "OPENALADDIN_EXECUTION_PROFILE": "analysis",
        "OPENALADDIN_MAME_SOUND": "none",
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
    if args.scheduler:
        environment["OPENALADDIN_TRACE_SCHEDULER"] = "1"
    if args.scheduler_calls:
        environment["OPENALADDIN_TRACE_SCHEDULER_CALLS"] = "1"

    status = run_shell_tool("mame/run.sh", [str(rom)], env=environment)
    if status == 0:
        if args.audio_commands:
            debug_log = ROOT / "debug.log"
            if debug_log.is_file():
                trace_dir.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(debug_log, trace_dir / "debug.log")
        if args.state_sync:
            synchronize_state_trace(trace_dir, rom_path=rom)
        print(f"trace: {trace_dir}")
        if args.state_output:
            print(f"state: {trace_dir / 'state.jsonl'}")
    return status

def command_audio_driver(args: argparse.Namespace) -> int:
    return run_tool(
        "genie/games/aladdin/mame/z80_sound.py",
        [str(resolve(args.rom)), "--output", str(resolve(args.output))],
    )

__all__ = [name for name in globals() if not name.startswith("__")]
