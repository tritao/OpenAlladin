from __future__ import annotations

from pathlib import Path

from genie.games.aladdin.analysis import actor_initializers as new_actor_initializers
from genie.games.aladdin.analysis import actors as new_actors
from genie.games.aladdin.analysis import audit_visual as new_audit_visual
from genie.games.aladdin.analysis import export_actor_records as new_export_records
from genie.games.aladdin.analysis import export_actor_timeline as new_export_timeline
from genie.games.aladdin.analysis import scenes as new_scenes
from genie.games.aladdin.analysis import transition_watch as new_transition_watch


ROOT = Path(__file__).resolve().parents[1]


def test_aladdin_analysis_modules_are_canonical():
    assert new_actor_initializers.main
    assert new_actors.main
    assert new_audit_visual.main
    assert new_export_records.main
    assert new_export_timeline.main
    assert new_scenes.analyze_scene_state_trace
    assert new_transition_watch.analyze_transition_watch
    assert new_actors.ROOT == ROOT
    assert new_actor_initializers.ROOT == ROOT
    assert new_audit_visual.ROOT == ROOT


def test_generic_visual_diff_stays_outside_game_package():
    from genie.analysis.visual_diff import compare

    assert callable(compare)
    assert not (ROOT / "genie/games/aladdin/analysis/visual_diff.py").exists()
