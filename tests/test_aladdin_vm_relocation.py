from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile

from genie.games.aladdin.vm import animation as new_animation
from genie.games.aladdin.vm import classify_animation as new_classify
from genie.games.aladdin.vm import movement as new_movement
from genie.games.aladdin.vm import verify as new_verify


ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "rom/Disneys_Aladdin_U_p1.bin"


def test_aladdin_vm_modules_are_canonical():
    assert new_animation.AnimationDecoder
    assert new_animation.RomReader
    assert new_movement.MovementDecoder
    assert new_classify.classify
    assert new_verify.check_record
    assert new_movement.ROOT == ROOT
    assert new_classify.ROOT == ROOT


def test_animation_entrypoint_is_canonical():
    with tempfile.TemporaryDirectory(prefix="aladdin-vm-relocation-") as directory:
        directory_path = Path(directory)
        output = directory_path / "animation.json"
        arguments = [
            str(ROM),
            "--output",
            str(output),
            "--max-instructions",
            "1",
            "--max-bytes",
            "32",
        ]
        subprocess.run(
            [sys.executable, str(ROOT / "genie/games/aladdin/vm/animation.py"), *arguments],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        assert output.is_file()


def test_movement_entrypoint_is_canonical():
    with tempfile.TemporaryDirectory(prefix="aladdin-vm-relocation-") as directory:
        directory_path = Path(directory)
        output = directory_path / "movement.json"
        arguments = [
            str(ROM),
            "--output",
            str(output),
            "--max-steps",
            "1",
            "--max-bytes",
            "32",
        ]
        subprocess.run(
            [sys.executable, str(ROOT / "genie/games/aladdin/vm/movement.py"), *arguments],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        assert output.is_file()
