from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile

from genie.games.aladdin.vm import animation as new_animation
from genie.games.aladdin.vm import classify_animation as new_classify
from genie.games.aladdin.vm import movement as new_movement
from genie.games.aladdin.vm import verify as new_verify
from genie.vm import animation as old_animation
from genie.vm import classify_animation as old_classify
from genie.vm import movement as old_movement
from genie.vm import verify as old_verify


ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "rom/Disneys_Aladdin_U_p1.bin"


def test_legacy_vm_modules_reexport_new_implementations():
    assert old_animation.AnimationDecoder is new_animation.AnimationDecoder
    assert old_animation.RomReader is new_animation.RomReader
    assert old_movement.MovementDecoder is new_movement.MovementDecoder
    assert old_classify.classify is new_classify.classify
    assert old_verify.check_record is new_verify.check_record
    assert new_movement.ROOT == ROOT
    assert new_classify.ROOT == ROOT


def test_legacy_and_new_animation_entrypoints_are_byte_identical():
    with tempfile.TemporaryDirectory(prefix="aladdin-vm-relocation-") as directory:
        directory_path = Path(directory)
        old_output = directory_path / "old-animation.json"
        new_output = directory_path / "new-animation.json"
        common = [
            str(ROM),
            "--output",
            str(old_output),
            "--max-instructions",
            "1",
            "--max-bytes",
            "32",
        ]
        subprocess.run(
            [sys.executable, str(ROOT / "genie/vm/animation.py"), *common],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        common[2] = str(new_output)
        subprocess.run(
            [sys.executable, str(ROOT / "genie/games/aladdin/vm/animation.py"), *common],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        assert old_output.read_bytes() == new_output.read_bytes()


def test_legacy_and_new_movement_entrypoints_are_byte_identical():
    with tempfile.TemporaryDirectory(prefix="aladdin-vm-relocation-") as directory:
        directory_path = Path(directory)
        old_output = directory_path / "old-movement.json"
        new_output = directory_path / "new-movement.json"
        common = [
            str(ROM),
            "--output",
            str(old_output),
            "--max-steps",
            "1",
            "--max-bytes",
            "32",
        ]
        subprocess.run(
            [sys.executable, str(ROOT / "genie/vm/movement.py"), *common],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        common[2] = str(new_output)
        subprocess.run(
            [sys.executable, str(ROOT / "genie/games/aladdin/vm/movement.py"), *common],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        assert old_output.read_bytes() == new_output.read_bytes()
