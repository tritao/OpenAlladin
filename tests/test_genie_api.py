from __future__ import annotations

import warnings

import genie.api as api
from genie.games.aladdin.mame.state import load_state_trace


def test_api_exposes_only_explicit_core_services():
    assert "DataIndex" in api.__all__
    assert "load_profile" in api.__all__
    assert "load_state_trace" not in api.__all__


def test_api_resolves_legacy_services_lazily():
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        assert api.load_state_trace is load_state_trace
    assert any(item.category is DeprecationWarning for item in caught)
