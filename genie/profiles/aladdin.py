"""The current Disney's Aladdin game profile."""

from __future__ import annotations

from .base import GameProfile


def _semantic_providers():
    # Keep the profile module lightweight and avoid a core-data/game-data
    # import cycle until a DataIndex is actually constructed.
    from genie.games.aladdin.data import semantic_providers

    return semantic_providers()


ALADDIN = GameProfile(
    id="aladdin",
    platform="genesis",
    default_rom="rom/Disneys_Aladdin_U_p1.bin",
    ghidra_config="re/config/ghidra.yml",
    experiments_manifest="re/mame/experiments/manifest.yml",
    events_manifest="re/mame/events/manifest.yml",
    input_format="openaladdin-input-v1",
    event_format="openaladdin-event-v1",
    state_formats=(
        "openaladdin-frame-state-v2",
        "openaladdin-frame-state-v3",
    ),
    parity_fields=(
        "player.x",
        "player.y",
        "player.world_x",
        "player.world_y",
        "player.vx",
        "player.vy",
        "player.animation_pc",
        "player.frame_ptr",
        "player.facing_x_flip",
        "player.animation_timer",
        "player.grounded",
        "scene.state",
        "camera.x",
        "camera.y",
        "camera.scroll_x",
        "camera.scroll_y",
        "actors",
    ),
    atomic_state_fields=("player", "camera", "terrain", "scene", "actors", "scheduler"),
    atomic_actor_fields=(
        "type",
        "x",
        "y",
        "movement_flags",
        "facing_x_flip",
        "facing_y_flip",
        "sprite_attribute",
        "frame_ptr",
        "animation_pc",
        "movement_pc",
        "movement_loop_pc",
        "movement_loop_timer",
        "movement_word_18",
        "movement_word_1a",
        "animation_timer",
        "movement_return_pc",
        "flags",
        "movement_command_timer",
        "collision_box",
    ),
    semantic_provider_factory=_semantic_providers,
)


__all__ = ["ALADDIN"]
