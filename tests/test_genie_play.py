from __future__ import annotations

from types import SimpleNamespace

from genie.cli import build_parser
import genie.commands.play as play
from genie.runtime import ROOT, default_rom


def test_play_parser_defaults_to_native():
    args = build_parser().parse_args(["play"])
    assert args.client is None
    assert args.client_option is None
    assert args.headless is False


def test_play_parser_accepts_mame_alias():
    args = build_parser().parse_args(["play", "--client", "mame", "--headless"])
    assert args.client_option == "mame"
    assert args.headless is True


def test_native_play_command_is_build_aware(monkeypatch):
    args = build_parser().parse_args(["play", "--headless", "--frames", "3"])
    captured = {}

    def fake_run(command, **kwargs):
        captured["command"] = command
        captured["kwargs"] = kwargs
        return SimpleNamespace(returncode=0)

    monkeypatch.setattr(play.subprocess, "run", fake_run)
    assert play.command_play(args) == 0
    assert captured["command"][:2] == [str(ROOT / "run.sh"), "--rom"]
    assert "--no-window" in captured["command"]
    assert captured["kwargs"]["cwd"] == ROOT


def test_mame_play_is_windowed_by_default(monkeypatch):
    args = build_parser().parse_args(["play", "mame"])
    captured = {}

    def fake_run_tool(name, arguments, *, env):
        captured["name"] = name
        captured["arguments"] = arguments
        captured["env"] = env
        return 0

    monkeypatch.setattr(play, "run_shell_tool", fake_run_tool)
    assert play.command_play(args) == 0
    assert captured["name"] == "mame/run.sh"
    assert captured["env"]["OPENALADDIN_MAME_HEADLESS"] == "0"
    assert captured["env"]["OPENALADDIN_MAME_VIDEO"] == "soft"
    assert captured["env"]["OPENALADDIN_TRACE_FRAMES"] == "-1"
    assert captured["env"]["OPENALADDIN_INPUT_MODE"] == "record"


def test_mame_play_headless_is_explicit():
    args = build_parser().parse_args(["play", "mame", "--headless", "--frames", "4"])
    environment, trace_dir = play._mame_environment(args, default_rom())
    assert trace_dir == ROOT / "build/re/play/mame"
    assert environment["OPENALADDIN_MAME_HEADLESS"] == "1"
    assert environment["OPENALADDIN_TRACE_FRAMES"] == "3"
    assert environment["OPENALADDIN_MAME_SOUND"] == "none"
