"""Stable core service surface for composing Genie workflows.

The historical module exported every name from runtime and the Aladdin MAME
services. Those names remain available through lazy compatibility lookup for
callers migrating to explicit imports, but new wildcard consumers receive only
the deliberately small core surface below.
"""

from __future__ import annotations

import importlib
import warnings
from typing import Any

from genie.data import DataIndex, Reference, SemanticDataClassifier
from genie.layout.model import Layout, LayoutRange
from genie.profiles import GameProfile, load_profile
from genie.semantic_coverage import build_semantic_coverage
from genie.symbols import Symbol, SymbolStore


_COMPAT_MODULES = (
    "genie.common",
    "genie.runtime",
    "genie.games.aladdin.mame.experiments",
    "genie.games.aladdin.mame.state",
    "genie.games.aladdin.mame.runs",
)


def __getattr__(name: str) -> Any:
    """Resolve legacy service names while directing callers to explicit imports."""

    value: Any = None
    source: str | None = None
    for module_name in _COMPAT_MODULES:
        module = importlib.import_module(module_name)
        if hasattr(module, name):
            value = getattr(module, name)
            source = module_name
    if source is None:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    warnings.warn(
        f"genie.api.{name} is deprecated; import it from {source} instead",
        DeprecationWarning,
        stacklevel=2,
    )
    return value


__all__ = [
    "DataIndex",
    "GameProfile",
    "Layout",
    "LayoutRange",
    "Reference",
    "SemanticDataClassifier",
    "Symbol",
    "SymbolStore",
    "build_semantic_coverage",
    "load_profile",
]
