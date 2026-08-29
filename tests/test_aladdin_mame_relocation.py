from __future__ import annotations

from genie.games.aladdin.mame import audio_parity as canonical_audio_parity
from genie.games.aladdin.mame import audio_trace as canonical_audio_trace
from genie.games.aladdin.mame import capture_matrix as canonical_capture_matrix
from genie.games.aladdin.mame import compare_actors as canonical_compare_actors
from genie.games.aladdin.mame import compare_collision as canonical_compare_collision
from genie.games.aladdin.mame import compare_scheduler as canonical_compare_scheduler
from genie.games.aladdin.mame import coverage_gaps as canonical_coverage_gaps
from genie.games.aladdin.mame import state as canonical
from genie.games.aladdin.mame import validate_level_loader_matrix as canonical_level_loader
from genie.games.aladdin.mame import z80_sound as canonical_z80_sound
from genie.common import ROOT, load_yaml, parse_int
def test_aladdin_mame_modules_are_canonical():
    assert canonical.load_state_trace
    for module in (
        canonical_audio_trace,
        canonical_audio_parity,
        canonical_z80_sound,
        canonical_compare_actors,
        canonical_compare_collision,
        canonical_compare_scheduler,
        canonical_coverage_gaps,
        canonical_level_loader,
        canonical_capture_matrix,
    ):
        assert module.main


def test_z80_driver_copy_contract_uses_exclusive_68k_end_pointer():
    knowledge = load_yaml(ROOT / "re/sound/z80-driver.yml")
    copy_map = knowledge["rom_copy"]
    start = canonical_z80_sound.DRIVER_ROM_START
    end = canonical_z80_sound.DRIVER_ROM_END
    assert end == 0x001B9D05
    assert end - start + 1 == 0x1886
    assert parse_int(copy_map["source_end_inclusive"]) == end
    assert parse_int(copy_map["size"]) == 0x1886
