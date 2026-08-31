from __future__ import annotations

from types import SimpleNamespace

import genie.commands.replay as replay


def test_clean_core_replay_uses_the_separate_deterministic_client(tmp_path, monkeypatch):
    run_dir = tmp_path / "run"
    run_dir.mkdir()
    rom = tmp_path / "game.bin"
    rom.write_bytes(b"rom")
    captured = {}

    monkeypatch.setattr(
        replay,
        "_load_run_manifest",
        lambda name: (run_dir, {"format": "test", "replays": {}}),
    )
    monkeypatch.setattr(replay, "_run_rom", lambda args, manifest: rom)
    monkeypatch.setattr(
        replay,
        "load_input_timeline",
        lambda path: (None, [{"frame": 0, "mask": 0}, {"frame": 1, "mask": 0}]),
    )
    monkeypatch.setattr(replay, "_update_replay_manifest", lambda *args: None)

    def fake_run(command, **kwargs):
        captured["command"] = command
        captured["kwargs"] = kwargs
        return SimpleNamespace(returncode=0)

    monkeypatch.setattr(replay.subprocess, "run", fake_run)
    args = SimpleNamespace(name="test", rom=None, segment=None, client="clean-core")

    assert replay.command_replay(args) == 0
    command = captured["command"]
    assert command[0].endswith("/run-clean-core.sh")
    assert command[command.index("--frames") + 1] == "2"
    assert command[command.index("--state-output") + 1].endswith(
        "/replay/clean-core/state.jsonl"
    )
    assert captured["kwargs"]["cwd"] == replay.ROOT
