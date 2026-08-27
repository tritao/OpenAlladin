#!/usr/bin/env python3
"""Smoke tests for the packaged Genie frontend and compatibility paths."""

from __future__ import annotations

import importlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from genie.common import (  # noqa: E402
    ProjectContext,
    attach_provenance,
    build_provenance,
    write_provenance_json,
)


def _run(*command: str) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        list(command),
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def _normalize_help(value: str) -> str:
    return "\n".join(" ".join(line.split()) for line in value.splitlines())


def main() -> int:
    module_names = (
        "genie.analysis.actors",
        "genie.assets.extract",
        "genie.ghidra.import_rom",
        "genie.mame.compare_state",
        "genie.vm.animation",
    )
    for module_name in module_names:
        importlib.import_module(module_name)

    context = ProjectContext.discover(ROOT)
    assert context.root == ROOT.resolve()
    assert context.rom == ROOT / "rom/Disneys_Aladdin_U_p1.bin"
    assert context.re_dir == ROOT / "re"
    assert context.mame == ROOT / "external/mame/mame"
    provenance = build_provenance(context, generated_at="2026-01-01T00:00:00Z", strict=False)
    assert set(provenance) == {
        "rom_sha256",
        "repository_commit",
        "mame_commit",
        "ghidra_version",
        "tool_version",
        "generated_at",
    }
    assert attach_provenance(
        {"format": "smoke-v1"},
        context,
        strict=False,
        generated_at=provenance["generated_at"],
    )["provenance"] == provenance
    with tempfile.TemporaryDirectory(prefix="genie-smoke-") as temporary:
        output = Path(temporary) / "artifact.json"
        write_provenance_json(output, {"format": "smoke-v1"}, context, strict=False, generated_at=provenance["generated_at"])
        assert json.loads(output.read_text(encoding="utf-8"))["provenance"] == provenance

    genie_help = _run(sys.executable, "-m", "genie", "--help")
    oa_help = _run(sys.executable, "tools/oa.py", "--help")
    shell_help = _run("./oa.sh", "--help")
    doctor_help = _run(sys.executable, "-m", "genie", "doctor", "--help")
    assert "usage: genie" in genie_help.stdout
    assert "usage: oa" in oa_help.stdout
    assert "usage: oa" in shell_help.stdout
    assert "doctor" in genie_help.stdout
    assert "--strict" in doctor_help.stdout
    assert "trace" in genie_help.stdout and "trace" in oa_help.stdout
    assert _normalize_help(genie_help.stdout).replace("usage: genie", "usage: oa", 1) == _normalize_help(oa_help.stdout)
    assert shell_help.stdout == oa_help.stdout

    print("Genie packaging smoke: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
