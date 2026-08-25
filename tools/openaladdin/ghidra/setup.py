#!/usr/bin/env python3
"""Download and install the pinned Ghidra release."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

from openaladdin.common import ROOT, load_yaml


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def check_java(required_major: int) -> None:
    try:
        result = subprocess.run(["java", "-version"], capture_output=True, text=True, check=False)
    except FileNotFoundError as exc:
        raise SystemExit("Java 21 or newer is required; java was not found on PATH") from exc
    version = result.stderr + result.stdout
    match = re.search(r'version "([0-9]+)', version)
    if not match or int(match.group(1)) < required_major:
        raise SystemExit(f"Java {required_major}+ is required; detected: {version.strip()}")
    print(f"Java OK: {match.group(1)}+")


def safe_extract(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    root = destination.resolve()
    with zipfile.ZipFile(archive) as zipped:
        for member in zipped.infolist():
            target = (destination / member.filename).resolve()
            if target != root and root not in target.parents:
                raise RuntimeError(f"unsafe archive member: {member.filename}")
        zipped.extractall(destination)


def ensure_launchers(install: Path) -> None:
    relative_paths = [
        "support/analyzeHeadless",
        "support/pyghidraRun",
        "support/launch.sh",
        "Ghidra/Features/Decompiler/os/linux_x86_64/sleigh",
        "Ghidra/Features/Decompiler/os/linux_x86_64/decompile",
        "Ghidra/Features/FileFormats/os/linux_x86_64/lzfse",
        "GPL/DemanglerGnu/os/linux_x86_64/demangler_gnu_v2_24",
        "GPL/DemanglerGnu/os/linux_x86_64/demangler_gnu_v2_41",
    ]
    for relative in relative_paths:
        launcher = install / relative
        if launcher.is_file():
            launcher.chmod(launcher.stat().st_mode | 0o111)


def install_release(config: dict) -> Path:
    ghidra = config["ghidra"]
    install = ROOT / ghidra["install_dir"]
    properties = install / "Ghidra/application.properties"
    if properties.is_file() and f"application.version={ghidra['version']}" in properties.read_text(encoding="utf-8"):
        ensure_launchers(install)
        print(f"Ghidra already installed: {install}")
        return install

    downloads = ROOT / ".tools/downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / ghidra["release_asset"]
    url = f"https://github.com/NationalSecurityAgency/ghidra/releases/download/{ghidra['release_tag']}/{ghidra['release_asset']}"
    if not archive.is_file() or sha256(archive) != ghidra["sha256"]:
        partial = archive.with_suffix(archive.suffix + ".part")
        print(f"Downloading {url}")
        urllib.request.urlretrieve(url, partial)
        actual = sha256(partial)
        if actual != ghidra["sha256"]:
            partial.unlink(missing_ok=True)
            raise SystemExit(f"Ghidra SHA-256 mismatch: expected {ghidra['sha256']}, got {actual}")
        partial.replace(archive)
    print(f"Ghidra archive OK: {archive}")

    staging = ROOT / ".tools/ghidra-extract"
    if staging.exists():
        shutil.rmtree(staging)
    safe_extract(archive, staging)
    candidates = [item for item in staging.iterdir() if item.is_dir()]
    if len(candidates) != 1:
        raise SystemExit(f"could not identify Ghidra directory in {archive}")
    if install.exists():
        shutil.rmtree(install)
    candidates[0].rename(install)
    shutil.rmtree(staging, ignore_errors=True)
    ensure_launchers(install)
    print(f"Installed Ghidra: {install}")
    return install


def install_pyghidra(install: Path) -> None:
    venv = ROOT / ".tools/venv"
    python = venv / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if not python.is_file():
        print(f"Creating PyGhidra environment: {venv}")
        subprocess.run([sys.executable, "-m", "venv", str(venv)], check=True)
    dist = install / "Ghidra/Features/PyGhidra/pypkg/dist"
    subprocess.run(
        [str(python), "-m", "pip", "install", "--no-index", "--disable-pip-version-check", "-f", str(dist), "pyghidra"],
        check=True,
    )
    print(f"PyGhidra ready: {python}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    args = parser.parse_args()
    config = load_yaml(ROOT / "re/config/ghidra.yml")
    check_java(int(config["java"]["major"]))
    install = install_release(config)
    install_pyghidra(install)
    print("Setup complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
