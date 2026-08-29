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
