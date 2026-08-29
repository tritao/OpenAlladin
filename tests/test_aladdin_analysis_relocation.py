from __future__ import annotations

from pathlib import Path

from genie.games.aladdin.analysis import actor_initializers as new_actor_initializers
from genie.games.aladdin.analysis import actors as new_actors
from genie.games.aladdin.analysis import audit_visual as new_audit_visual
from genie.games.aladdin.analysis import export_actor_records as new_export_records
from genie.games.aladdin.analysis import export_actor_timeline as new_export_timeline
from genie.games.aladdin.analysis import scenes as new_scenes
from genie.games.aladdin.analysis import transition_watch as new_transition_watch
from genie.analysis import actor_initializers as old_actor_initializers
from genie.analysis import actors as old_actors
from genie.analysis import audit_visual as old_audit_visual
from genie.analysis import export_actor_records as old_export_records
from genie.analysis import export_actor_timeline as old_export_timeline
from genie.analysis import scenes as old_scenes
from genie.analysis import transition_watch as old_transition_watch


ROOT = Path(__file__).resolve().parents[1]


def test_legacy_analysis_modules_reexport_new_implementations():
    assert old_actor_initializers.main is new_actor_initializers.main
    assert old_actors.main is new_actors.main
    assert old_audit_visual.main is new_audit_visual.main
    assert old_export_records.main is new_export_records.main
    assert old_export_timeline.main is new_export_timeline.main
    assert old_scenes.analyze_scene_state_trace is new_scenes.analyze_scene_state_trace
    assert old_transition_watch.analyze_transition_watch is new_transition_watch.analyze_transition_watch
    assert new_actors.ROOT == ROOT
    assert new_actor_initializers.ROOT == ROOT
    assert new_audit_visual.ROOT == ROOT


def test_generic_visual_diff_stays_outside_game_package():
    from genie.analysis.visual_diff import compare

    assert callable(compare)
    assert not (ROOT / "genie/games/aladdin/analysis/visual_diff.py").exists()
