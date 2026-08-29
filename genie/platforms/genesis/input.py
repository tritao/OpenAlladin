"""Genesis three-button controller mapping and input-token helpers."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


INPUT_BUTTONS = ("up", "down", "left", "right", "a", "b", "c", "start")
INPUT_MASKS = {name: 1 << index for index, name in enumerate(INPUT_BUTTONS)}
INPUT_MAPPING = "mame-genesis-3button-v1"

# Early recordings labelled the raw Mega Drive bits linearly as A/B/C. MAME's
# physical fields are B/C/A at those bit positions.
LEGACY_BUTTON_REMAP = {"a": "b", "b": "c", "c": "a"}


@dataclass(frozen=True, slots=True)
class Genesis3ButtonInput:
    """Portable logical representation of a Genesis three-button pad."""

    buttons: tuple[str, ...] = INPUT_BUTTONS
    masks: dict[str, int] | None = None
    mapping: str = INPUT_MAPPING

    def __post_init__(self) -> None:
        if self.masks is None:
            object.__setattr__(
                self,
                "masks",
                {name: 1 << index for index, name in enumerate(self.buttons)},
            )

    def buttons_for_mask(self, mask: int) -> list[str]:
        if mask < 0 or mask > 0xFF:
            raise SystemExit(f"input mask out of range: {mask}")
        assert self.masks is not None
        return [name for name in self.buttons if mask & self.masks[name]]

    def token_for_mask(self, mask: int) -> str:
        buttons = self.buttons_for_mask(mask)
        return "+".join(buttons) if buttons else "none"

    def client_input_tokens(
        self,
        header: dict[str, Any] | None,
        tokens: list[str],
    ) -> list[str]:
        """Translate recordings made before the controller mapping marker."""

        if header and header.get("controller_mapping") is not None:
            return list(tokens)
        return [
            "+".join(LEGACY_BUTTON_REMAP.get(part, part) for part in token.split("+"))
            if token != "none" else token
            for token in tokens
        ]


GENESIS_3BUTTON_INPUT = Genesis3ButtonInput()


def buttons_for_mask(mask: int) -> list[str]:
    return GENESIS_3BUTTON_INPUT.buttons_for_mask(mask)


def token_for_mask(mask: int) -> str:
    return GENESIS_3BUTTON_INPUT.token_for_mask(mask)


def client_input_tokens(
    header: dict[str, Any] | None,
    tokens: list[str],
) -> list[str]:
    return GENESIS_3BUTTON_INPUT.client_input_tokens(header, tokens)


__all__ = [
    "Genesis3ButtonInput",
    "GENESIS_3BUTTON_INPUT",
    "INPUT_BUTTONS",
    "INPUT_MASKS",
    "INPUT_MAPPING",
    "LEGACY_BUTTON_REMAP",
    "buttons_for_mask",
    "client_input_tokens",
    "token_for_mask",
]
