"""Game profiles supported by Genie."""

from __future__ import annotations

from .aladdin import ALADDIN
from .base import GameProfile


_PROFILES = {ALADDIN.id: ALADDIN}


def load_profile(profile_id: str | None = None) -> GameProfile:
    """Load a profile by id, defaulting to the current Aladdin profile.

    The optional id is deliberately programmatic for now.  Command-line
    profile selection can be added later without making the runtime depend on
    an ambient environment variable.
    """

    selected_id = ALADDIN.id if profile_id is None else profile_id
    try:
        return _PROFILES[selected_id]
    except KeyError as error:
        known = ", ".join(sorted(_PROFILES))
        raise ValueError(f"unknown Genie profile {selected_id!r}; known profiles: {known}") from error


__all__ = ["ALADDIN", "GameProfile", "load_profile"]
