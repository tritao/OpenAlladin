from __future__ import annotations

import genie.api as api


def test_api_exposes_only_explicit_core_services():
    assert "DataIndex" in api.__all__
    assert "load_profile" in api.__all__
    assert "load_state_trace" not in api.__all__


def test_api_does_not_reexport_game_services():
    try:
        api.load_state_trace
    except AttributeError:
        pass
    else:
        raise AssertionError("game services must be imported from their package")
