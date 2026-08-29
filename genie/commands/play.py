"""Launch the native or MAME gameplay client."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess

from genie.common import hashes
from genie.core.mame.runner import run_shell_tool
from genie.games.aladdin.mame.runs import _clean_mame_environment
from genie.runtime import ROOT, resolve


def _client(args: argparse.Namespace) -> str:
    positional = getattr(args, "client", None)
    option = getattr(args, "client_option", None)
    if positional and option and positional != option:
        raise SystemExit("play client was specified twice with different values")
    return option or positional or "native"


def _native_arguments(args: argparse.Namespace, rom: Path) -> list[str]:
    if args.trace_dir or args.video != "soft" or args.debug_ui or args.load_state:
        raise SystemExit("the selected play option is only supported by the MAME client")
    command = [str(ROOT / "run.sh"), "--rom", str(rom)]
    if args.level_index is not None:
        command.extend(["--level-index", str(args.level_index)])
    if args.frames is not None:
        command.extend(["--frames", str(args.frames)])
    if args.headless:
        command.append("--no-window")
    if args.no_audio:
        command.append("--no-audio")
    if args.input is not None:
        command.extend(["--input-schedule", args.input])
    if args.demo:
        command.append("--demo")
    return command


def _mame_environment(args: argparse.Namespace, rom: Path) -> tuple[dict[str, str], Path]:
    if args.level_index is not None or args.demo:
        raise SystemExit("--level-index and --demo are only supported by the native client")
    trace_dir = resolve(args.trace_dir) if args.trace_dir else ROOT / "build/re/play/mame"
    environment = _clean_mame_environment()
    environment.update({
        "OPENALADDIN_TRACE_DIR": str(trace_dir),
        "OPENALADDIN_TRACE_FRAMES": "-1" if args.frames is None else str(args.frames - 1),
        "OPENALADDIN_CAPTURE": "none",
        "OPENALADDIN_STATE_OUTPUT": "0",
        "OPENALADDIN_INPUT_MODE": "inject" if args.input is not None else "record",
        "OPENALADDIN_MAME_HEADLESS": "1" if args.headless else "0",
        "OPENALADDIN_MAME_VIDEO": args.video,
        "OPENALADDIN_MAME_DEBUG_UI": "1" if args.debug_ui else "0",
        "OPENALADDIN_MAME_SOUND": "none" if args.headless or args.no_audio else "sdl",
        "OPENALADDIN_EXECUTION_PROFILE": "analysis" if args.headless else "interactive",
        "OPENALADDIN_ROM_SHA256": hashes(rom)["sha256"],
    })
    if args.input is not None:
        environment["OPENALADDIN_INPUT"] = args.input
    if args.load_state:
        environment["OPENALADDIN_LOAD_STATE"] = str(resolve(Path(args.load_state)))
    return environment, trace_dir


def command_play(args: argparse.Namespace) -> int:
    rom = resolve(args.rom)
    if not rom.is_file():
        raise SystemExit(f"ROM not found: {rom}")
    if args.frames is not None and args.frames < 1:
        raise SystemExit("--frames must be positive when supplied")

    client = _client(args)
    if client == "native":
        command = _native_arguments(args, rom)
        print("play: launching native OpenAladdin")
        return subprocess.run(command, cwd=ROOT, check=False).returncode

    environment, trace_dir = _mame_environment(args, rom)
    mode = "headless" if args.headless else "windowed"
    print(f"play: launching {mode} MAME for {rom}")
    print(f"play: trace output {trace_dir}")
    return run_shell_tool("mame/run.sh", [str(rom)], env=environment)


__all__ = ["command_play"]
