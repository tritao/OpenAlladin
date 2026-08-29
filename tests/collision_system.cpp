#include "collision.hpp"

#include <cassert>
#include <vector>

int main() {
    openaladdin::ActorSystem geometry_actors;
    openaladdin::ActorLifecycleSystem geometry_lifecycle(geometry_actors);
    openaladdin::CollisionSystem geometry(geometry_lifecycle);
    std::vector<std::uint8_t> rom(0x40, 0);
    constexpr std::uint32_t frame = 0x10;
    rom[frame + 2] = 2;
    rom[frame + 3] = 3;
    rom[frame + 4] = 8;
    rom[frame + 5] = 12;
    geometry.bind_rom(rom);

    const auto display = geometry.frame_bounds(frame, 100, 200, false);
    assert(display.valid);
    assert(display.left == 102 && display.top == 203);
    assert(display.right == 108 && display.bottom == 212);

    const auto mirrored_display = geometry.frame_bounds(frame, 100, 200, true);
    assert(mirrored_display.left == 92 && mirrored_display.right == 98);
    const auto mirrored_hitbox = geometry.hitbox(frame, 100, 200, true);
    assert(mirrored_hitbox.left == 348 && mirrored_hitbox.right == 354);

    const openaladdin::CollisionBox first{true, 0, 0, 10, 10};
    const openaladdin::CollisionBox touching{true, 10, 0, 20, 10};
    const openaladdin::CollisionBox overlapping{true, 9, 0, 20, 10};
    assert(!openaladdin::CollisionSystem::overlaps(first, touching));
    assert(openaladdin::CollisionSystem::overlaps(first, overlapping));
    assert(!openaladdin::CollisionSystem::strict_overlaps(first, touching));
    assert(openaladdin::CollisionSystem::strict_overlaps(first, overlapping));

    openaladdin::GameState state;
    openaladdin::ActorLifecycleSystem lifecycle(state.actors);
    openaladdin::CollisionSystem collision(lifecycle);
    lifecycle.bind_rom(rom);
    collision.bind_rom(rom);
    state.player.x = 0;
    state.player.y = 0;
    state.camera.x = 0;
    state.camera.y = 0;
    state.actors[1].type = 0x2D;
    state.actors[1].frame_ptr = frame;
    const auto effects = collision.player_actor(
        state,
        openaladdin::PlayerCollisionInput{frame, false, false, false}
    );
    assert(effects.player_collision_interaction_pending);
    assert(state.actors[1].type == 0);

    state.actors[25].type = 0x2D;
    state.actors[25].frame_ptr = frame;
    const auto auxiliary_effects = collision.player_actor(
        state,
        openaladdin::PlayerCollisionInput{frame, false, false, false}
    );
    assert(!auxiliary_effects.player_collision_interaction_pending);
    assert(state.actors[25].type == 0x2D);
    return 0;
}
