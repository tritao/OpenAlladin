from __future__ import annotations

from genie.games.aladdin.mame import audio_parity as canonical_audio_parity
from genie.games.aladdin.mame import audio_trace as canonical_audio_trace
from genie.games.aladdin.mame import state as canonical
from genie.games.aladdin.mame import z80_sound as canonical_z80_sound
from genie.mame import audio_parity as legacy_audio_parity
from genie.mame import audio_trace as legacy_audio_trace
from genie.mame import state as legacy
from genie.mame import z80_sound as legacy_z80_sound


def test_legacy_mame_state_reexports_aladdin_implementation():
    assert legacy.load_state_trace is canonical.load_state_trace
    assert legacy.normalize_animation_state_trace is canonical.normalize_animation_state_trace
    assert legacy.synchronize_state_trace is canonical.synchronize_state_trace
    assert legacy.compress_input_schedule is canonical.compress_input_schedule


def test_legacy_mame_audio_reexports_aladdin_implementations():
    assert legacy_audio_trace.summarize is canonical_audio_trace.summarize
    assert legacy_audio_trace.main is canonical_audio_trace.main
    assert legacy_audio_parity.compare_records is canonical_audio_parity.compare_records
    assert legacy_audio_parity.main is canonical_audio_parity.main
    assert legacy_z80_sound.build_report is canonical_z80_sound.build_report
    assert legacy_z80_sound.main is canonical_z80_sound.main
