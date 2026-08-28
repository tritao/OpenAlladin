"""Pinned external toolchain support for byte-exact 68000 deassembly."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import re
import shutil
import subprocess
from tempfile import TemporaryDirectory

from genie.common.helpers import load_yaml


class ToolchainError(RuntimeError):
    """Raised when the configured assembler toolchain is unavailable."""


@dataclass(frozen=True, slots=True)
class M68kToolchain:
    name: str
    version: str
    assembler: Path
    linker: Path
    nm: Path
    objcopy: Path
    flags: tuple[str, ...]


def _config(root: Path) -> dict:
    value = load_yaml(root / "re/config/deasm.yml") or {}
    if not isinstance(value, dict):
        raise ToolchainError("re/config/deasm.yml must contain a mapping")
    toolchain = value.get("toolchain") or {}
    if not isinstance(toolchain, dict):
        raise ToolchainError("deasm toolchain configuration must be a mapping")
    return toolchain


def _configured_path(root: Path, name: str, environment: str) -> Path | None:
    override = os.environ.get(environment)
    if override:
        return Path(override).expanduser()
    path = shutil.which(name)
    if path:
        return Path(path)
    # Keep a future checkout-local toolchain discoverable without making it a
    # prerequisite. The supported installation is the system package.
    local = root / ".tools" / "m68k-binutils-2.42" / "bin" / name
    return local if local.is_file() else None


def _version(executable: Path) -> str:
    try:
        result = subprocess.run(
            [str(executable), "--version"],
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError as error:
        raise ToolchainError(f"could not execute {executable}: {error}") from error
    output = result.stdout or result.stderr
    match = re.search(r"(?:Binutils|assembler)[^\n]*\s([0-9]+(?:\.[0-9]+)+)\s*$", output, re.IGNORECASE | re.MULTILINE)
    if result.returncode != 0 or not match:
        raise ToolchainError(f"could not determine binutils version from {executable}")
    return match.group(1)


def find_toolchain(root: Path) -> M68kToolchain:
    """Resolve and validate the configured GNU m68k assembler pair."""

    root = Path(root)
    config = _config(root)
    assembler_name = str(config.get("assembler", "m68k-linux-gnu-as"))
    linker_name = str(config.get("linker", "m68k-linux-gnu-ld"))
    nm_name = str(config.get("nm", "m68k-linux-gnu-nm"))
    objcopy_name = str(config.get("objcopy", "m68k-linux-gnu-objcopy"))
    assembler = _configured_path(root, assembler_name, "GENIE_M68K_AS")
    linker = _configured_path(root, linker_name, "GENIE_M68K_LD")
    nm = _configured_path(root, nm_name, "GENIE_M68K_NM")
    objcopy = _configured_path(root, objcopy_name, "GENIE_M68K_OBJCOPY")
    if assembler is None:
        raise ToolchainError(
            f"missing {assembler_name}; install GNU m68k binutils, for example: "
            "sudo apt-get install binutils-m68k-linux-gnu"
        )
    if objcopy is None:
        raise ToolchainError(
            f"missing {objcopy_name}; install GNU m68k binutils, for example: "
            "sudo apt-get install binutils-m68k-linux-gnu"
        )
    if linker is None:
        raise ToolchainError(
            f"missing {linker_name}; install GNU m68k binutils, for example: "
            "sudo apt-get install binutils-m68k-linux-gnu"
        )
    if nm is None:
        raise ToolchainError(
            f"missing {nm_name}; install GNU m68k binutils, for example: "
            "sudo apt-get install binutils-m68k-linux-gnu"
        )
    version = _version(assembler)
    expected = str(config.get("version", ""))
    if expected and version != expected:
        raise ToolchainError(
            f"{assembler} is binutils {version}; configured deasm toolchain is {expected}"
        )
    flags = config.get("flags") or ["-m68000"]
    if not isinstance(flags, list):
        raise ToolchainError("deasm toolchain flags must be a list")
    return M68kToolchain(
        name=str(config.get("name", "GNU binutils m68k")),
        version=version,
        assembler=assembler,
        linker=linker,
        nm=nm,
        objcopy=objcopy,
        flags=tuple(str(flag) for flag in flags),
    )


@dataclass(frozen=True, slots=True)
class AssemblyResult:
    binary: bytes
    stdout: str = ""
    stderr: str = ""
    symbols: dict[str, int] | None = None


class AssemblyError(ToolchainError):
    """The assembler rejected generated source."""

    def __init__(self, message: str, *, line_number: int | None = None, stderr: str = ""):
        super().__init__(message)
        self.line_number = line_number
        self.stderr = stderr


def assemble(
    source: str,
    toolchain: M68kToolchain,
    *,
    symbol_prefix: str | None = None,
) -> AssemblyResult:
    """Assemble source and extract its flat ``.text`` section."""

    with TemporaryDirectory(prefix="genie-deasm-") as directory:
        root = Path(directory)
        source_path = root / "aladdin.s"
        object_path = root / "aladdin.o"
        binary_path = root / "aladdin.bin"
        source_path.write_text(source, encoding="utf-8")
        try:
            assembled = subprocess.run(
                [str(toolchain.assembler), *toolchain.flags, "-o", str(object_path), str(source_path)],
                capture_output=True,
                text=True,
                check=False,
            )
        except OSError as error:
            raise ToolchainError(f"could not execute assembler {toolchain.assembler}: {error}") from error
        if assembled.returncode != 0:
            line_number = None
            match = re.search(r"aladdin\.s:(\d+):", assembled.stderr)
            if match:
                line_number = int(match.group(1))
            detail = assembled.stderr.strip() or assembled.stdout.strip() or "assembler failed"
            raise AssemblyError(detail, line_number=line_number, stderr=assembled.stderr)
        try:
            extracted = subprocess.run(
                [
                    str(toolchain.linker),
                    "--entry=0",
                    "-Ttext=0",
                    "--oformat=binary",
                    "-o",
                    str(binary_path),
                    str(object_path),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
        except OSError as error:
            raise ToolchainError(f"could not execute objcopy {toolchain.objcopy}: {error}") from error
        if extracted.returncode != 0:
            detail = extracted.stderr.strip() or extracted.stdout.strip() or "linker failed"
            raise ToolchainError(detail)
        try:
            binary = binary_path.read_bytes()
        except OSError as error:
            raise ToolchainError(f"linker did not produce {binary_path}: {error}") from error
        symbols: dict[str, int] | None = None
        if symbol_prefix is not None:
            try:
                symbol_listing = subprocess.run(
                    [str(toolchain.nm), "-a", "-n", str(object_path)],
                    capture_output=True,
                    text=True,
                    check=False,
                )
            except OSError as error:
                raise ToolchainError(f"could not execute nm {toolchain.nm}: {error}") from error
            if symbol_listing.returncode != 0:
                detail = symbol_listing.stderr.strip() or symbol_listing.stdout.strip() or "nm failed"
                raise ToolchainError(detail)
            symbols = {}
            pattern = re.compile(r"^([0-9A-Fa-f]+)\s+\S\s+(%s\S*)$" % re.escape(symbol_prefix))
            for line in symbol_listing.stdout.splitlines():
                match = pattern.match(line.strip())
                if match:
                    symbols[match.group(2)] = int(match.group(1), 16)
        return AssemblyResult(binary, assembled.stdout, assembled.stderr, symbols)


__all__ = [
    "AssemblyError",
    "AssemblyResult",
    "M68kToolchain",
    "ToolchainError",
    "assemble",
    "find_toolchain",
]
