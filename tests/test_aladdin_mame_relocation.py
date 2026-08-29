from __future__ import annotations

from genie.games.aladdin.mame import state as canonical
from genie.mame import state as legacy


def test_legacy_mame_state_reexports_aladdin_implementation():
    assert legacy.load_state_trace is canonical.load_state_trace
    assert legacy.normalize_animation_state_trace is canonical.normalize_animation_state_trace
    assert legacy.synchronize_state_trace is canonical.synchronize_state_trace
    assert legacy.compress_input_schedule is canonical.compress_input_schedule
