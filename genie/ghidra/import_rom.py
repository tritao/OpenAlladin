#!/usr/bin/env python3
"""Create a clean, deterministic headless Ghidra project for the Genesis ROM."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from genie.common import (
    ROOT,
    hashes,
    load_yaml,
    normalize_symbols,
    normalize_types,
    parse_int,
    rom_entries,
    write_json,
    write_mame_symbols,
)


VECTOR_NAMES = [
    "InitialStackPointer",
    "Reset",
    "BusError",
    "AddressError",
    "IllegalInstruction",
    "ZeroDivide",
    "CHKInstruction",
    "TRAPVInstruction",
    "PrivilegeViolation",
    "Trace",
    "Line1010Emulator",
    "Line1111Emulator",
    "FormatError",
    "UninitializedInterrupt",
    "Reserved14",
    "Reserved15",
    "Reserved16",
    "Reserved17",
    "Reserved18",
    "Reserved19",
    "Reserved20",
    "Reserved21",
    "Reserved22",
    "Reserved23",
    "SpuriousInterrupt",
    "Level1Interrupt",
    "Level2Interrupt",
    "Level3Interrupt",
    "Level4Interrupt",
    "Level5Interrupt",
    "VBlankInterrupt",
    "Level7Interrupt",
]
VECTOR_NAMES.extend(f"TRAP{i}" for i in range(16))


def resolve_rom(requested: Path | None, expected: dict) -> Path:
    candidates = []
    if requested:
        candidates.append(requested)
    candidates.extend(
        [
            ROOT / str(expected.get("expected_filename", "rom/Disneys_Aladdin_U_p1.bin")),
            ROOT / "rom/aladdin-usa.bin",
        ]
    )
    for candidate in candidates:
        path = candidate if candidate.is_absolute() else Path.cwd() / candidate
        if path.is_file():
            return path.resolve()
    searched = "\n".join(str(item.resolve()) for item in candidates)
    raise SystemExit(f"ROM not found. Searched:\n{searched}")


def make_analysis_config(rom: Path, output_dir: Path) -> Path:
    _, expected, _ = rom_entries()
    memory_map = load_yaml(ROOT / "re/config/memory_map.yml") or {}
    for block in memory_map.get("blocks", []):
        block["start"] = parse_int(block["start"])
        if block.get("end") == "rom_end":
            block["end"] = rom.stat().st_size - 1
        else:
            block["end"] = parse_int(block["end"])
    symbols = normalize_symbols()
    analysis = {
        "rom": str(rom),
        "rom_size": rom.stat().st_size,
        "rom_identity": {**expected, **hashes(rom)},
        "memory_map": memory_map,
        "vectors": [{"index": index, "name": name} for index, name in enumerate(VECTOR_NAMES)],
        "symbols": symbols,
        "types": normalize_types(),
        "export_dir": str(output_dir),
        "mame_symbols": str(output_dir / "mame_symbols.lua"),
    }
    path = output_dir / "analysis.json"
    write_json(path, analysis)
    write_mame_symbols(output_dir / "mame_symbols.lua", symbols)
    return path


def ghidra_install() -> Path:
    config = load_yaml(ROOT / "re/config/ghidra.yml")
    return ROOT / config["ghidra"]["install_dir"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path, nargs="?", help="ROM path; defaults to the existing local Aladdin dump")
    parser.add_argument("--allow-unverified", action="store_true", help="allow a ROM not listed in re/config/roms.yml")
    parser.add_argument("--reuse-project", action="store_true", help="do not delete the existing local project")
    parser.add_argument("--no-analysis", action="store_true", help="import and run scripts without Ghidra auto-analysis")
    args = parser.parse_args()

    _, expected, _ = rom_entries()
    rom = resolve_rom(args.rom, expected)
    verifier = ROOT / "genie/ghidra/verify.py"
    verify_command = [sys.executable, str(verifier), str(rom)]
    if args.allow_unverified:
        verify_command.append("--allow-unverified")
    subprocess.run(verify_command, check=True)

    output_dir = ROOT / "build/re"
    output_dir.mkdir(parents=True, exist_ok=True)
    analysis_config = make_analysis_config(rom, output_dir)

    config = load_yaml(ROOT / "re/config/ghidra.yml")
    project_dir = ROOT / config["ghidra"]["project_dir"]
    project_name = config["ghidra"]["project_name"]
    project_dir.mkdir(parents=True, exist_ok=True)
    if not args.reuse_project:
        project_files = [
            project_dir / project_name,
            project_dir / f"{project_name}.gpr",
            project_dir / f"{project_name}.rep",
        ]
        for project_file in project_files:
            if project_file.is_dir():
                shutil.rmtree(project_file)
            elif project_file.exists():
                project_file.unlink()

    headless = ghidra_install() / "support" / ("pyghidraRun.bat" if os.name == "nt" else "pyghidraRun")
    if not headless.is_file():
        raise SystemExit(f"Ghidra is not installed. Run: python genie/ghidra/setup.py\nMissing: {headless}")
    scripts = ROOT / "re/ghidra/scripts"
    command = [
        str(headless),
        "-H",
        str(project_dir),
        project_name,
        "-import",
        str(rom),
        "-loader",
        "BinaryLoader",
        "-loader-baseAddr",
        "0x0",
        "-loader-blockName",
        "ROM",
        "-processor",
        config["ghidra"]["language"],
        "-scriptPath",
        str(scripts),
        "-preScript",
        "ImportGenesis.py",
        str(analysis_config),
        "-preScript",
        "ParseVectors.py",
        str(analysis_config),
        "-preScript",
        "ApplySymbols.py",
        str(analysis_config),
        "-preScript",
        "ApplyTypes.py",
        str(analysis_config),
        "-postScript",
        "ExportSymbols.py",
        str(analysis_config),
    ]
    if args.no_analysis:
        command.insert(4, "-noanalysis")
    print("Running:", " ".join(command))
    environment = os.environ.copy()
    venv_bin = ROOT / ".tools/venv" / ("Scripts" if os.name == "nt" else "bin")
    if venv_bin.is_dir():
        environment["PATH"] = str(venv_bin) + os.pathsep + environment.get("PATH", "")
    environment["GHIDRA_INSTALL_DIR"] = str(ghidra_install())
    environment["OPENALADDIN_ROOT"] = str(ROOT)
    subprocess.run(command, cwd=ROOT, env=environment, check=True)
    print(f"Analysis complete: {project_dir / project_name}")
    print(f"Exports: {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
