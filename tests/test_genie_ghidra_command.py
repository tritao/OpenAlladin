from pathlib import Path
import json

from genie.cli import build_parser
from genie.commands import deasm
from genie.commands import data
from genie.commands import ghidra
from genie.ghidra.database import AnalysisDatabase
from genie.ghidra.decompile import decompile_function


def test_ghidra_subcommands_dispatch_to_ghidra_command_module():
    setup = build_parser().parse_args(["ghidra", "setup"])
    verify = build_parser().parse_args(["ghidra", "verify", "rom.bin", "--allow-unverified"])
    rebuild = build_parser().parse_args(["ghidra", "rebuild", "--rom", "rom.bin", "--no-analysis"])
    scan = build_parser().parse_args(["ghidra", "scan", "--rom", "rom.bin", "--allow-unverified"])
    validate_db = build_parser().parse_args(["ghidra", "validate-db", "--json"])
    layout = build_parser().parse_args(["layout", "show", "0x1234", "--json"])
    layout_candidates = build_parser().parse_args(["layout", "candidates", "--limit", "3", "--json"])
    deasm_build = build_parser().parse_args(["deasm", "build"])
    deasm_stats = build_parser().parse_args(["deasm", "stats", "--json"])
    deasm_todo = build_parser().parse_args(["deasm", "todo", "--limit", "3"])
    context = build_parser().parse_args(["ghidra", "context", "0x1234", "--json"])
    decompile = build_parser().parse_args(["ghidra", "decompile", "0x1234", "--force"])
    data_decode = build_parser().parse_args(["data", "decode", "0x1234", "--json"])

    assert setup.function is ghidra.command_ghidra_setup
    assert verify.function is ghidra.command_ghidra_verify
    assert verify.rom == Path("rom.bin")
    assert verify.allow_unverified is True
    assert rebuild.function is ghidra.command_ghidra_rebuild
    assert rebuild.no_analysis is True
    assert scan.function is ghidra.command_ghidra_scan
    assert scan.allow_unverified is True
    assert validate_db.function is ghidra.command_ghidra_validate_db
    assert validate_db.json_output is True
    assert layout.address == 0x1234
    assert layout_candidates.limit == 3
    assert layout_candidates.json_output is True
    assert deasm_build.function is deasm.command_deasm_build
    assert deasm_stats.function is deasm.command_deasm_stats
    assert deasm_stats.json_output is True
    assert deasm_todo.function is deasm.command_deasm_todo
    assert deasm_todo.limit == 3
    assert context.function is ghidra.command_ghidra_context
    assert context.radius == 2
    assert decompile.function is ghidra.command_ghidra_decompile
    assert decompile.force is True
    assert data_decode.function is data.command_data_decode
    assert data_decode.json_output is True


def test_ghidra_rebuild_calls_existing_service(monkeypatch, capsys):
    calls = []

    monkeypatch.setattr(ghidra, "resolve", lambda path: Path("rom.bin"))
    monkeypatch.setattr(
        ghidra,
        "rebuild_project",
        lambda rom, **options: calls.append((rom, options)) or 0,
    )
    monkeypatch.setattr(ghidra, "validate_knowledge", lambda rom: [])

    args = build_parser().parse_args([
        "ghidra",
        "rebuild",
        "--rom",
        "rom.bin",
        "--allow-unverified",
        "--reuse-project",
        "--no-analysis",
    ])
    assert args.function(args) == 0

    assert calls == [
        (
            Path("rom.bin"),
            {"allow_unverified": True, "reuse_project": True, "no_analysis": True},
        ),
    ]
    assert "validated symbols and types" in capsys.readouterr().out


def test_ghidra_scan_calls_scan_service(monkeypatch, capsys):
    calls = []

    monkeypatch.setattr(ghidra, "resolve", lambda path: Path("rom.bin"))
    monkeypatch.setattr(
        ghidra,
        "scan_project",
        lambda rom, **options: calls.append((rom, options)) or 0,
    )
    monkeypatch.setattr(ghidra, "validate_knowledge", lambda rom: [])

    args = build_parser().parse_args([
        "ghidra",
        "scan",
        "--rom",
        "rom.bin",
        "--allow-unverified",
        "--reuse-project",
        "--no-analysis",
    ])
    assert args.function(args) == 0
    assert calls == [
        (
            Path("rom.bin"),
            {"allow_unverified": True, "reuse_project": True, "no_analysis": True},
        ),
    ]
    assert "validated symbols and types" in capsys.readouterr().out


def test_single_function_decompile_reuses_cached_pseudocode(tmp_path):
    database_root = tmp_path / "full-rom"
    database_root.mkdir()
    (database_root / "metadata.json").write_text(json.dumps({"rom": {}, "rom_size": 0x40}), encoding="utf-8")
    (database_root / "functions.json").write_text(
        json.dumps([{"address": "0x00000010", "start": "0x00000010", "end": "0x00000018", "name": "CachedFunction"}]),
        encoding="utf-8",
    )
    cache = database_root / "decompile"
    cache.mkdir()
    cached = cache / "00000010.txt"
    cached.write_text("void CachedFunction(void) {}\n", encoding="utf-8")

    result = decompile_function(0x14, database=AnalysisDatabase(database_root))

    assert result["status"] == "cached"
    assert result["address"] == "0x00000010"
    assert result["path"] == str(cached)
