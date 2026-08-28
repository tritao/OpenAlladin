from pathlib import Path

from genie.cli import build_parser
from genie.commands import ghidra


def test_ghidra_subcommands_dispatch_to_ghidra_command_module():
    setup = build_parser().parse_args(["ghidra", "setup"])
    verify = build_parser().parse_args(["ghidra", "verify", "rom.bin", "--allow-unverified"])
    rebuild = build_parser().parse_args(["ghidra", "rebuild", "--rom", "rom.bin", "--no-analysis"])
    scan = build_parser().parse_args(["ghidra", "scan", "--rom", "rom.bin", "--allow-unverified"])
    validate_db = build_parser().parse_args(["ghidra", "validate-db", "--json"])
    layout = build_parser().parse_args(["layout", "show", "0x1234", "--json"])

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
