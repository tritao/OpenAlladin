#!/usr/bin/env python3
"""Download the pinned Ghidra release and build the Genesis loader extension."""

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

from common import ROOT, load_yaml


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


def build_loader(config: dict, install: Path, skip: bool) -> None:
    if skip:
        print("Skipping Genesis loader build")
        return
    loader_source = ROOT / config["submodules"]["genesis_loader"]
    loader = ROOT / ".tools/ghidra-loader"
    if loader.exists():
        shutil.rmtree(loader)
    shutil.copytree(
        loader_source,
        loader,
        ignore=shutil.ignore_patterns(".git", "build", ".gradle"),
    )
    compatibility_patch = ROOT / "third_party/patches/ghidra_sega_ldr-ghidra12.patch"
    patch_tool = shutil.which("patch")
    if patch_tool:
        subprocess.run([patch_tool, "-p1", "--forward", "--batch", "-i", str(compatibility_patch)], cwd=loader, check=True)
    else:
        source = loader / "src/main/java/sega/SegaLoader.java"
        text = source.read_text(encoding="utf-8")
        old_import = "import ghidra.app.util.opinion.AbstractLibrarySupportLoader;\n"
        old_signature = "protected void load(ByteProvider provider, LoadSpec loadSpec, List<Option> options, Program program, TaskMonitor monitor, MessageLog log) throws IOException {"
        if "Loader.ImporterSettings" not in text:
            text = text.replace(old_import, old_import + "import ghidra.app.util.opinion.Loader.ImporterSettings;\n", 1)
        if old_signature not in text:
            raise SystemExit("Genesis loader compatibility patch did not match SegaLoader.java")
        replacement = """protected void load(Program program, ImporterSettings settings) throws IOException {

		ByteProvider provider = settings.provider();
		LoadSpec loadSpec = settings.loadSpec();
		List<Option> options = settings.options();
		TaskMonitor monitor = settings.monitor();
		MessageLog log = settings.log();"""
        source.write_text(text.replace(old_signature, replacement, 1), encoding="utf-8")
    gradle = shutil.which("gradle")
    if gradle is None:
        wrapper = ROOT / config["submodules"]["ghidra_source"] / "gradlew"
        if not wrapper.is_file():
            raise SystemExit("Gradle is required to build the Genesis loader")
        gradle_command = [str(wrapper)]
    else:
        gradle_command = [gradle]
    env = os.environ.copy()
    env["GHIDRA_INSTALL_DIR"] = str(install)
    print("Building Genesis loader extension")
    subprocess.run(gradle_command + ["-PGHIDRA_INSTALL_DIR=" + str(install)], cwd=loader, env=env, check=True)
    zips = sorted((loader / "build").glob("**/*.zip"))
    dist_zips = sorted((loader / "dist").glob("*.zip"))
    if dist_zips:
        zips = dist_zips
    if not zips:
        raise SystemExit("Genesis loader build completed without producing an extension zip")
    extension_dir = install / "Ghidra/Extensions"
    extension_dir.mkdir(parents=True, exist_ok=True)
    # Older bootstrap attempts may have selected Gradle's source archive.
    # Remove only that known generated artifact before installing the real zip.
    shutil.rmtree(extension_dir / "src", ignore_errors=True)
    with zipfile.ZipFile(zips[-1]) as zipped:
        zipped.extractall(extension_dir)
    print(f"Genesis loader installed from {zips[-1].name}")


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
    parser.add_argument("--skip-loader", action="store_true", help="install Ghidra without building the optional loader")
    args = parser.parse_args()
    config = load_yaml(ROOT / "re/config/ghidra.yml")
    check_java(int(config["java"]["major"]))
    install = install_release(config)
    install_pyghidra(install)
    build_loader(config, install, args.skip_loader)
    print("Setup complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
