"""Genie command-line parser and dispatch."""

from __future__ import annotations

import argparse
from collections.abc import Sequence

from genie.core.mame.runner import run_tool
from genie.runtime import *
from genie.commands.ghidra import *
from genie.commands.layout import *
from genie.commands.deasm import *
from genie.commands.symbols import *
from genie.commands.setup import *
from genie.commands.play import *
from genie.commands.trace import *
from genie.commands.record import *
from genie.commands.replay import *
from genie.commands.parity import *
from genie.commands.misc import *
from genie.commands.data import *
from genie.commands.coverage_report import *
def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="genie",
        description=__doc__,
    )
    commands = parser.add_subparsers(dest="command", required=True)

    setup = commands.add_parser("setup", help="install the pinned Ghidra toolchain")
    setup.set_defaults(function=command_setup)

    verify = commands.add_parser("verify", help="verify the configured ROM identity")
    add_rom_argument(verify, positional=True)
    verify.add_argument("--allow-unverified", action="store_true")
    verify.set_defaults(function=command_verify)

    ghidra = commands.add_parser("ghidra", help="manage Ghidra analysis")
    ghidra_commands = ghidra.add_subparsers(dest="ghidra_command", required=True)
    ghidra_setup = ghidra_commands.add_parser("setup", help="install the pinned Ghidra toolchain")
    ghidra_setup.set_defaults(function=command_ghidra_setup)
    ghidra_verify = ghidra_commands.add_parser("verify", help="verify a ROM before analysis")
    add_rom_argument(ghidra_verify, positional=True)
    ghidra_verify.add_argument("--allow-unverified", action="store_true")
    ghidra_verify.set_defaults(function=command_ghidra_verify)
    rebuild = ghidra_commands.add_parser("rebuild", help="verify and rebuild the local project")
    add_rom_argument(rebuild)
    rebuild.add_argument("--allow-unverified", action="store_true")
    rebuild.add_argument("--reuse-project", action="store_true")
    rebuild.add_argument("--no-analysis", action="store_true")
    rebuild.set_defaults(function=command_ghidra_rebuild)
    scan = ghidra_commands.add_parser("scan", help="rebuild and export the whole-ROM analysis database")
    add_rom_argument(scan)
    scan.add_argument("--allow-unverified", action="store_true")
    scan.add_argument("--reuse-project", action="store_true")
    scan.add_argument("--no-analysis", action="store_true")
    scan.set_defaults(function=command_ghidra_scan)

    def add_analysis_query(name: str, help_text: str, function) -> argparse.ArgumentParser:
        query = ghidra_commands.add_parser(name, help=help_text)
        query.add_argument("address", type=lambda value: int(value, 0))
        query.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
        query.add_argument("--json", action="store_true", dest="json_output")
        query.set_defaults(function=function)
        return query

    add_analysis_query("function", "show a function from the whole-ROM database", command_ghidra_function)
    add_analysis_query("callers", "show callers of a function", command_ghidra_callers)
    add_analysis_query("callees", "show callees of a function", command_ghidra_callees)
    add_analysis_query("writers", "show writes to an address", command_ghidra_writers)
    add_analysis_query("readers", "show reads from an address", command_ghidra_readers)
    add_analysis_query("xrefs", "show references to an address", command_ghidra_xrefs)
    vm_writers = ghidra_commands.add_parser(
        "vm-writers",
        help="show encoded AnimationVM writes to a global RAM address",
    )
    vm_writers.add_argument("address", type=lambda value: int(value, 0))
    add_rom_argument(vm_writers)
    vm_writers.add_argument(
        "--layout",
        type=Path,
        default=ROOT / "build/re/full-rom/layout.json",
    )
    vm_writers.add_argument("--json", action="store_true", dest="json_output")
    vm_writers.set_defaults(function=command_ghidra_vm_writers)
    decompile = ghidra_commands.add_parser(
        "decompile",
        help="decompile and cache one function or the semantic review queue",
    )
    decompile.add_argument("address", type=lambda value: int(value, 0), nargs="?")
    decompile.add_argument(
        "--review",
        action="store_true",
        help="batch-decompile the named functions with open semantic review questions",
    )
    decompile.add_argument(
        "--limit",
        type=int,
        default=0,
        help="maximum review functions to decompile; zero means all",
    )
    decompile.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    decompile.add_argument("--cache-dir", type=Path)
    decompile.add_argument("--project-dir", type=Path)
    decompile.add_argument("--force", action="store_true", help="refresh an existing cached pseudocode")
    decompile.add_argument("--json", action="store_true", dest="json_output")
    decompile.set_defaults(function=command_ghidra_decompile)
    context = ghidra_commands.add_parser(
        "context",
        help="show the combined RE context for an address or the review queue",
    )
    context.add_argument("address", type=lambda value: int(value, 0), nargs="?")
    context.add_argument(
        "--review",
        action="store_true",
        help="show context for named functions with open semantic-review questions",
    )
    context.add_argument(
        "--limit",
        type=int,
        default=0,
        help="maximum review functions to show; zero means all",
    )
    context.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    context.add_argument("--layout", type=Path)
    context.add_argument("--coverage", type=Path)
    context.add_argument("--radius", type=int, default=2, help="nearby layout objects on each side")
    context.add_argument(
        "--include-decompile",
        action="store_true",
        help="include cached pseudocode; never invokes Ghidra",
    )
    context.add_argument("--json", action="store_true", dest="json_output")
    context.set_defaults(function=command_ghidra_context)
    unknown = ghidra_commands.add_parser("unknown", help="show unclassified ROM ranges")
    unknown.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    unknown.add_argument("--json", action="store_true", dest="json_output")
    unknown.set_defaults(function=command_ghidra_unknown)
    validate_db = ghidra_commands.add_parser(
        "validate-db",
        help="validate a generated whole-ROM database against known ROM facts",
    )
    validate_db.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    validate_db.add_argument("--json", action="store_true", dest="json_output")
    validate_db.set_defaults(function=command_ghidra_validate_db)

    layout = commands.add_parser("layout", help="classify and query the ROM layout")
    layout_commands = layout.add_subparsers(dest="layout_command", required=True)
    layout_build = layout_commands.add_parser("build", help="build the normalized ROM layout")
    layout_build.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    layout_build.add_argument("--output", type=Path)
    layout_build.add_argument("--no-artifacts", action="store_true", help="use only Ghidra and tracked-symbol evidence")
    layout_build.set_defaults(function=command_layout_build)
    layout_show = layout_commands.add_parser("show", help="show the range containing an address")
    layout_show.add_argument("address", type=lambda value: int(value, 0))
    layout_show.add_argument("--layout", type=Path, default=ROOT / "build/re/full-rom/layout.json")
    layout_show.add_argument("--json", action="store_true", dest="json_output")
    layout_show.set_defaults(function=command_layout_show)
    layout_gaps = layout_commands.add_parser("gaps", help="show unknown ROM ranges")
    layout_gaps.add_argument("--layout", type=Path, default=ROOT / "build/re/full-rom/layout.json")
    layout_gaps.add_argument("--json", action="store_true", dest="json_output")
    layout_gaps.set_defaults(function=command_layout_gaps)
    layout_stats = layout_commands.add_parser("stats", help="show layout class counts and byte totals")
    layout_stats.add_argument("--layout", type=Path, default=ROOT / "build/re/full-rom/layout.json")
    layout_stats.add_argument("--json", action="store_true", dest="json_output")
    layout_stats.set_defaults(function=command_layout_stats)
    layout_validate = layout_commands.add_parser("validate", help="validate layout coverage and range ownership")
    layout_validate.add_argument("--layout", type=Path, default=ROOT / "build/re/full-rom/layout.json")
    layout_validate.add_argument("--json", action="store_true", dest="json_output")
    layout_validate.set_defaults(function=command_layout_validate)
    layout_candidates = layout_commands.add_parser(
        "candidates",
        help="rank unknown layout gaps using offline references and decoder evidence",
    )
    layout_candidates.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    layout_candidates.add_argument("--layout", type=Path, default=ROOT / "build/re/full-rom/layout.json")
    layout_candidates.add_argument("--rom", type=Path, default=default_rom())
    layout_candidates.add_argument("--animation", type=Path, help="override decoded animation-stream report")
    layout_candidates.add_argument("--movement", type=Path, help="override decoded movement-stream report")
    layout_candidates.add_argument("--limit", type=int, default=50, help="maximum rows; zero means all")
    layout_candidates.add_argument("--max-references", type=int, default=12, help="evidence rows retained per candidate")
    layout_candidates.add_argument(
        "--strong-only",
        action="store_true",
        help="hide gaps supported only by data-to-data references",
    )
    layout_candidates.add_argument("--json", action="store_true", dest="json_output")
    layout_candidates.set_defaults(function=command_layout_candidates)

    deasm = commands.add_parser("deasm", help="generate and inspect the complete ROM deassembly")
    deasm_commands = deasm.add_subparsers(dest="deasm_command", required=True)

    deasm_build = deasm_commands.add_parser("build", help="emit a complete symbolized ROM source")
    deasm_build.add_argument("--rom", type=Path, default=default_rom())
    deasm_build.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    deasm_build.add_argument("--layout", type=Path)
    deasm_build.add_argument("--instructions", type=Path)
    deasm_build.add_argument("--output", type=Path, default=ROOT / "build/re/deasm/aladdin.asm")
    deasm_build.add_argument("--metrics", type=Path, default=ROOT / "build/re/deasm/metrics.json")
    deasm_build.set_defaults(function=command_deasm_build)

    deasm_verify = deasm_commands.add_parser(
        "verify",
        help="assemble the generated source and compare it byte-for-byte with the ROM",
    )
    deasm_verify.add_argument("--rom", type=Path, default=default_rom())
    deasm_verify.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    deasm_verify.add_argument("--layout", type=Path)
    deasm_verify.add_argument("--instructions", type=Path)
    deasm_verify.add_argument("--output", type=Path, default=ROOT / "build/re/deasm/aladdin.asm")
    deasm_verify.add_argument(
        "--rebuilt",
        type=Path,
        default=ROOT / "build/re/deasm/aladdin-rebuilt.bin",
    )
    deasm_verify.add_argument("--metrics", type=Path, default=ROOT / "build/re/deasm/metrics.json")
    deasm_verify.set_defaults(function=command_deasm_verify)

    deasm_stats = deasm_commands.add_parser("stats", help="show deassembly coverage and naming statistics")
    deasm_stats.add_argument("--rom", type=Path, default=default_rom())
    deasm_stats.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    deasm_stats.add_argument("--layout", type=Path)
    deasm_stats.add_argument("--instructions", type=Path)
    deasm_stats.add_argument("--metrics", type=Path, default=ROOT / "build/re/deasm/metrics.json")
    deasm_stats.add_argument("--json", action="store_true", dest="json_output")
    deasm_stats.set_defaults(function=command_deasm_stats)

    deasm_todo = deasm_commands.add_parser(
        "todo",
        help="rank mechanically named functions for semantic investigation",
    )
    deasm_todo.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    deasm_todo.add_argument("--coverage", type=Path)
    deasm_todo.add_argument(
        "--semantic",
        action="store_true",
        help="include stable type-based names that still need semantic refinement",
    )
    deasm_todo.add_argument("--limit", type=int, default=25, help="maximum rows; zero means all")
    deasm_todo.add_argument("--json", action="store_true", dest="json_output")
    deasm_todo.set_defaults(function=command_deasm_todo)

    symbols = commands.add_parser("symbols", help="query canonical tracked symbols")
    symbol_commands = symbols.add_subparsers(dest="symbols_command", required=True)
    symbols_show = symbol_commands.add_parser("show", help="show the symbol at an address")
    symbols_show.add_argument("address", type=parse_symbol_address)
    symbols_show.add_argument("--json", action="store_true", dest="json_output")
    symbols_show.set_defaults(function=command_symbols_show)
    symbols_find = symbol_commands.add_parser("find", help="find symbols by name or alias")
    symbols_find.add_argument("query")
    symbols_find.add_argument("--kind", choices=("function", "ram", "data"))
    symbols_find.add_argument("--exact", action="store_true")
    symbols_find.add_argument("--json", action="store_true", dest="json_output")
    symbols_find.set_defaults(function=command_symbols_find)
    symbols_list = symbol_commands.add_parser("list", help="list canonical symbols")
    symbols_list.add_argument("--kind", choices=("function", "ram", "data"))
    symbols_list.add_argument("--json", action="store_true", dest="json_output")
    symbols_list.set_defaults(function=command_symbols_list)
    symbols_validate = symbol_commands.add_parser("validate", help="validate tracked symbol maps")
    symbols_validate.add_argument("--rom", type=Path)
    symbols_validate.add_argument("--json", action="store_true", dest="json_output")
    symbols_validate.set_defaults(function=command_symbols_validate)
    symbols_stats = symbol_commands.add_parser("stats", help="show symbol counts and confidence totals")
    symbols_stats.add_argument("--json", action="store_true", dest="json_output")
    symbols_stats.set_defaults(function=command_symbols_stats)

    symbols_unknown = symbol_commands.add_parser(
        "unknown",
        help="list mechanically named functions ranked by analysis evidence",
    )
    symbols_unknown.add_argument("--kind", choices=("function",), default="function")
    symbols_unknown.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    symbols_unknown.add_argument("--coverage", type=Path)
    symbols_unknown.add_argument(
        "--semantic",
        action="store_true",
        help="include stable type-based names that still need semantic refinement",
    )
    symbols_unknown.add_argument("--limit", type=int, default=0, help="maximum rows; zero means all")
    symbols_unknown.add_argument("--json", action="store_true", dest="json_output")
    symbols_unknown.set_defaults(function=command_symbols_unknown)

    symbols_next = symbol_commands.add_parser(
        "next",
        help="show the highest-priority mechanically named function",
    )
    symbols_next.add_argument("--kind", choices=("function",), default="function")
    symbols_next.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    symbols_next.add_argument("--coverage", type=Path)
    symbols_next.add_argument(
        "--semantic",
        action="store_true",
        help="include stable type-based names that still need semantic refinement",
    )
    symbols_next.add_argument("--json", action="store_true", dest="json_output")
    symbols_next.set_defaults(function=command_symbols_next)

    symbols_review = symbol_commands.add_parser(
        "review",
        help="rank named symbols whose semantic descriptions retain open questions",
    )
    symbols_review.add_argument("--kind", choices=("function", "ram", "data"))
    symbols_review.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    symbols_review.add_argument("--coverage", type=Path)
    symbols_review.add_argument("--limit", type=int, default=25, help="maximum rows; zero means all")
    symbols_review.add_argument("--json", action="store_true", dest="json_output")
    symbols_review.set_defaults(function=command_symbols_review)

    symbols_rename = symbol_commands.add_parser("rename", help="rename or create a canonical symbol")
    symbols_rename.add_argument("address", type=parse_symbol_address)
    symbols_rename.add_argument("name")
    symbols_rename.add_argument("--kind", choices=("function", "ram", "data"))
    symbols_rename.add_argument("--json", action="store_true", dest="json_output")
    symbols_rename.set_defaults(function=command_symbols_rename)

    symbols_describe = symbol_commands.add_parser("describe", help="set a symbol description")
    symbols_describe.add_argument("address", type=parse_symbol_address)
    symbols_describe.add_argument("description")
    symbols_describe.add_argument("--kind", choices=("function", "ram", "data"))
    symbols_describe.add_argument("--json", action="store_true", dest="json_output")
    symbols_describe.set_defaults(function=command_symbols_describe)

    symbols_confidence = symbol_commands.add_parser("confidence", help="set symbol confidence")
    symbols_confidence.add_argument("address", type=parse_symbol_address)
    symbols_confidence.add_argument("confidence")
    symbols_confidence.add_argument("--kind", choices=("function", "ram", "data"))
    symbols_confidence.add_argument("--json", action="store_true", dest="json_output")
    symbols_confidence.set_defaults(function=command_symbols_confidence)

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

    play = commands.add_parser(
        "play",
        help="launch the native or MAME gameplay client",
    )
    play.add_argument(
        "client",
        nargs="?",
        choices=("native", "mame"),
        help="client to launch (default: native)",
    )
    play.add_argument("--client", dest="client_option", choices=("native", "mame"))
    add_rom_argument(play)
    play.add_argument("--frames", type=int, help="stop after this many frames")
    play.add_argument("--headless", action="store_true", help="run without a visible window")
    play.add_argument("--no-audio", action="store_true", help="disable client audio")
    play.add_argument("--input", help="deterministic input schedule")
    play.add_argument("--demo", action="store_true", help="run the native demo input")
    play.add_argument("--level-index", type=int, help="native level index")
    play.add_argument("--trace-dir", type=Path, help="MAME trace output directory")
    play.add_argument("--video", default="soft", help="MAME video backend (default: soft)")
    play.add_argument("--debug-ui", action="store_true", help="show the MAME debugger UI")
    play.add_argument("--load-state", help="MAME save-state file to load")
    play.set_defaults(function=command_play)

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
    trace.add_argument("--scheduler", action="store_true", help="trace recovered frame phases and scheduler writer provenance")
    trace.add_argument("--scheduler-calls", action="store_true", help="trace every statically recovered gameplay call site and scheduler latch write")
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

    audio_parity = commands.add_parser(
        "audio-parity",
        help="compare normalized MAME and native audio traces",
    )
    audio_parity.add_argument("mame_trace", type=Path)
    audio_parity.add_argument("native_trace", type=Path)
    audio_parity.add_argument("--section", choices=("writes", "commands", "all"), default="all")
    audio_parity.add_argument("--mame-source", default="z80")
    audio_parity.add_argument("--native-frame-offset", type=int, default=0)
    audio_parity.set_defaults(function=lambda args: run_tool(
        "games/aladdin/mame/audio_parity.py",
        [str(resolve(args.mame_trace)), str(resolve(args.native_trace)),
         "--section", args.section,
         "--mame-source", args.mame_source,
         "--native-frame-offset", str(args.native_frame_offset)],
    ))

    scheduler_compare = commands.add_parser(
        "scheduler-compare",
        help="compare normalized MAME and native scheduler traces",
    )
    scheduler_compare.add_argument("genesis", type=Path)
    scheduler_compare.add_argument("openaladdin", type=Path)
    scheduler_compare.add_argument(
        "--phase",
        action="append",
        dest="phases",
        help="compare only this normalized phase family; repeat for a projection",
    )
    scheduler_compare.add_argument("--include-pcs", action="store_true")
    scheduler_compare.add_argument("--include-writers", action="store_true")
    scheduler_compare.add_argument("--right-frame-offset", type=int, default=0)
    scheduler_compare.add_argument("--intersection", action="store_true")
    scheduler_compare.add_argument("--start-frame", type=int)
    scheduler_compare.add_argument("--end-frame", type=int)
    scheduler_compare.set_defaults(function=command_scheduler_compare)

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

    doctor = commands.add_parser(
        "doctor",
        help="check the workspace and report fatal versus optional capabilities",
    )
    add_rom_argument(doctor)
    doctor.add_argument(
        "--strict",
        action="store_true",
        help="treat missing optional capabilities as fatal",
    )
    doctor.add_argument(
        "--json",
        action="store_true",
        dest="json_output",
        help="write the diagnostic report as JSON",
    )
    doctor.set_defaults(function=command_doctor)

    compare = commands.add_parser("compare", help="find the first divergent frame in two state traces")
    compare.add_argument("genesis", type=Path)
    compare.add_argument("openaladdin", type=Path)
    compare.add_argument(
        "--field",
        action="append",
        dest="fields",
        help="compare only this dotted state field; repeat for multiple fields",
    )
    compare.add_argument("--require-left-atomic", action="store_true")
    compare.add_argument("--require-atomic", action="store_true")
    compare.add_argument("--atomic-only", action="store_true")
    compare.add_argument("--left-atomic-only", action="store_true")
    compare.set_defaults(function=command_compare)

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
        "games/aladdin/mame/compare_collision.py",
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
    coverage_reads = coverage_commands.add_parser(
        "reads",
        help="map ROM-read tap records to canonical symbols and consumers",
    )
    coverage_reads.add_argument("trace_dirs", nargs="*", type=Path)
    coverage_reads.add_argument("--trace-root", type=Path, default=ROOT / "build/re/traces")
    coverage_reads.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    coverage_reads.add_argument("--output", type=Path, default=ROOT / "build/re/rom-reads.json")
    coverage_reads.add_argument("--json", action="store_true", dest="json_output")
    coverage_reads.set_defaults(function=command_coverage_reads)

    coverage_report = coverage_commands.add_parser(
        "report",
        help="report semantic function and ROM-data coverage",
    )
    coverage_report.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
    coverage_report.add_argument("--layout", type=Path, default=ROOT / "build/re/full-rom/layout.json")
    coverage_report.add_argument("--coverage", type=Path)
    coverage_report.add_argument("--animation", type=Path, help="override decoded animation-stream report")
    coverage_report.add_argument("--movement", type=Path, help="override decoded movement-stream report")
    coverage_report.add_argument("--json", action="store_true", dest="json_output")
    coverage_report.set_defaults(function=command_coverage_report)

    data = commands.add_parser(
        "data",
        help="query semantic ROM objects and their evidence",
    )
    data_commands = data.add_subparsers(dest="data_command", required=True)

    def add_data_sources(query: argparse.ArgumentParser) -> None:
        query.add_argument("--database", type=Path, default=ROOT / "build/re/full-rom")
        query.add_argument("--layout", type=Path, default=ROOT / "build/re/full-rom/layout.json")
        query.add_argument("--coverage", type=Path)
        query.add_argument("--animation", type=Path, help="override decoded animation-stream report")
        query.add_argument("--movement", type=Path, help="override decoded movement-stream report")

    data_stats = data_commands.add_parser("stats", help="show ROM-object and evidence counts")
    add_data_sources(data_stats)
    data_stats.add_argument("--json", action="store_true", dest="json_output")
    data_stats.set_defaults(function=command_data_stats)

    data_todo = data_commands.add_parser("todo", help="rank ROM objects needing semantic evidence")
    add_data_sources(data_todo)
    data_todo.add_argument("--kind", choices=DATA_KINDS, default="all")
    data_todo.add_argument(
        "--rom-only",
        action="store_true",
        help="exclude RAM and hardware-address objects from the queue",
    )
    data_todo.add_argument(
        "--unresolved-only",
        action="store_true",
        help="show only unknown and provisional objects",
    )
    data_todo.add_argument("--limit", type=int, default=25, help="maximum rows; zero means all")
    data_todo.add_argument("--json", action="store_true", dest="json_output")
    data_todo.set_defaults(function=command_data_todo)

    data_next = data_commands.add_parser("next", help="show the highest-priority ROM data object")
    add_data_sources(data_next)
    data_next.add_argument("--kind", choices=DATA_KINDS, default="all")
    data_next.add_argument(
        "--rom-only",
        action="store_true",
        help="exclude RAM and hardware-address objects from the queue",
    )
    data_next.add_argument(
        "--unresolved-only",
        action="store_true",
        help="show only unknown and provisional objects",
    )
    data_next.add_argument("--json", action="store_true", dest="json_output")
    data_next.set_defaults(function=command_data_next)

    data_context = data_commands.add_parser("context", help="show semantic evidence for one ROM object")
    data_context.add_argument("address", type=lambda value: int(value, 0))
    add_data_sources(data_context)
    data_context.add_argument("--json", action="store_true", dest="json_output")
    data_context.set_defaults(function=command_data_context)
    data_decode = data_commands.add_parser(
        "decode",
        help="show cached or canonical VM instructions for one ROM stream",
    )
    data_decode.add_argument("address", type=lambda value: int(value, 0))
    add_data_sources(data_decode)
    data_decode.add_argument("--json", action="store_true", dest="json_output")
    data_decode.set_defaults(function=command_data_decode)

    status = commands.add_parser("status", help="show repository and RE progress status")
    add_rom_argument(status)
    status.set_defaults(function=lambda args: print_status(resolve(args.rom)))
    return parser
def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return int(args.function(args))


if __name__ == "__main__":
    raise SystemExit(main())
