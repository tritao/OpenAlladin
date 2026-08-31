#!/usr/bin/env python3
"""Regression tests for the MAME wrapper's display isolation policy."""

from __future__ import annotations

import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "genie/mame/run.sh"
ROM = ROOT / "rom/Disneys_Aladdin_U_p1.bin"


FAKE_MAME = """#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys

Path(os.environ["FAKE_MAME_LOG"]).write_text(
    json.dumps({
        "argv": sys.argv[1:],
        "sdl_video_driver": os.environ.get("SDL_VIDEODRIVER"),
    }),
    encoding="utf-8",
)
"""


FAKE_XVFB_RUN = """#!/usr/bin/env python3
import os
import subprocess
import sys

args = sys.argv[1:]
while args and args[0].startswith("-"):
    if args[0] == "-s":
        args = args[2:]
    else:
        args = args[1:]
assert args and args[0] == "env"
args = args[1:]
environment = dict(os.environ)
while args and "=" in args[0]:
    name, value = args.pop(0).split("=", 1)
    environment[name] = value
raise SystemExit(subprocess.run(args, env=environment, check=False).returncode)
"""


def executable(path: Path, contents: str) -> None:
    path.write_text(contents, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def run_wrapper(environment: dict[str, str], directory: Path) -> dict[str, object]:
    result = subprocess.run(
        [str(RUNNER), str(ROM)],
        cwd=ROOT,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    return json.loads((directory / "mame.json").read_text(encoding="utf-8"))


def base_environment(fake_mame: Path, log: Path, trace: Path) -> dict[str, str]:
    environment = dict(os.environ)
    environment.update(
        {
            "MAME_BIN": str(fake_mame),
            "FAKE_MAME_LOG": str(log),
            "OPENALADDIN_TRACE_DIR": str(trace),
            "OPENALADDIN_MAME_HEADLESS": "1",
            "OPENALADDIN_MAME_DEBUG_UI": "0",
            "MAME_XVFB": "0",
            # Prove the wrapper overrides an inherited interactive backend.
            "SDL_VIDEODRIVER": "x11",
        }
    )
    return environment


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="openaladdin-mame-test-") as name:
        directory = Path(name)
        fake_mame = directory / "fake-mame"
        executable(fake_mame, FAKE_MAME)

        log = directory / "mame.json"
        trace = directory / "trace"
        record = run_wrapper(base_environment(fake_mame, log, trace), directory)
        assert record["sdl_video_driver"] == "dummy"
        assert "-nowindow" not in record["argv"]
        assert "-window" not in record["argv"]
        assert record["argv"][record["argv"].index("-video") + 1] == "none"
        assert record["argv"][record["argv"].index("-seconds_to_run") + 1] == "3"

        interactive_environment = dict(base_environment(fake_mame, log, directory / "interactive-trace"))
        interactive_environment["OPENALADDIN_MAME_HEADLESS"] = "0"
        interactive_environment["OPENALADDIN_EXECUTION_PROFILE"] = "interactive"
        record = run_wrapper(interactive_environment, directory)
        assert record["sdl_video_driver"] == "x11"
        assert "-window" in record["argv"]
        assert "-nothrottle" not in record["argv"]
        assert record["argv"][record["argv"].index("-sound") + 1] == "sdl"

        debug_environment = base_environment(fake_mame, log, directory / "debug-trace")
        debug_environment["OPENALADDIN_STATE_SYNC"] = "1"
        record = run_wrapper(debug_environment, directory)
        assert record["sdl_video_driver"] == "dummy"
        assert "-debug" in record["argv"]
        assert record["argv"][record["argv"].index("-debugger") + 1] == "none"

        fake_xvfb = directory / "xvfb-run"
        executable(fake_xvfb, FAKE_XVFB_RUN)
        xvfb_environment = base_environment(fake_mame, log, directory / "xvfb-trace")
        xvfb_environment["MAME_XVFB"] = "1"
        xvfb_environment["PATH"] = f"{directory}{os.pathsep}{xvfb_environment['PATH']}"
        record = run_wrapper(xvfb_environment, directory)
        assert record["sdl_video_driver"] == "x11"
        assert record["argv"][record["argv"].index("-video") + 1] == "soft"

    print("MAME headless wrapper: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
