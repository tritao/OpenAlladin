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
from genie.mame import audio_parity as legacy_audio_parity
from genie.mame import audio_trace as legacy_audio_trace
from genie.mame import capture_matrix as legacy_capture_matrix
from genie.mame import compare_actors as legacy_compare_actors
from genie.mame import compare_collision as legacy_compare_collision
from genie.mame import compare_scheduler as legacy_compare_scheduler
from genie.mame import coverage_gaps as legacy_coverage_gaps
from genie.mame import state as legacy
from genie.mame import validate_level_loader_matrix as legacy_level_loader
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


def test_legacy_mame_diagnostics_reexport_aladdin_implementations():
    for legacy_module, canonical_module in (
        (legacy_compare_actors, canonical_compare_actors),
        (legacy_compare_collision, canonical_compare_collision),
        (legacy_compare_scheduler, canonical_compare_scheduler),
        (legacy_coverage_gaps, canonical_coverage_gaps),
        (legacy_level_loader, canonical_level_loader),
        (legacy_capture_matrix, canonical_capture_matrix),
    ):
        assert legacy_module.main is canonical_module.main
