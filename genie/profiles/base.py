"""Profile contracts for game-specific Genie configuration."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable


def _empty_semantic_providers() -> tuple[Any, ...]:
    return ()


@dataclass(frozen=True, slots=True)
class GameProfile:
    """Static configuration that varies between supported games.

    Paths are repository-relative.  Keeping them as data rather than resolving
    them here lets the profile stay independent of workspace discovery and
    keeps it usable by callers working with a different project root.
    """

    id: str
    platform: str

    default_rom: str
    ghidra_config: str

    experiments_manifest: str
    events_manifest: str

    input_format: str
    event_format: str
    state_formats: tuple[str, ...]

    parity_fields: tuple[str, ...]
    atomic_state_fields: tuple[str, ...]
    atomic_actor_fields: tuple[str, ...]

    # These identifiers are currently consumed by the shared runtime too.
    # They remain here so a future profile does not have to inherit Aladdin's
    # artifact vocabulary.  Controller details will move to the platform
    # layer in the next extraction phase.
    input_mapping: str = "mame-genesis-3button-v1"
    segments_format: str = "openaladdin-segments-v1"
    run_format: str = "openaladdin-input-run-v1"
    semantic_provider_factory: Callable[[], tuple[Any, ...]] = _empty_semantic_providers

    def semantic_providers(self) -> tuple[Any, ...]:
        """Instantiate semantic evidence providers for this profile."""

        return tuple(self.semantic_provider_factory())
