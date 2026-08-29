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

    const auto launch_handler = collision.player_collision_handler(0x11);
    assert(launch_handler.address == 0x001AF110);
    assert(launch_handler.kind
        == openaladdin::PlayerCollisionHandlerKind::PlayerLaunch);
    assert(!launch_handler.native_implemented);

    const auto noop_handler = collision.player_collision_handler(0x0D);
    assert(noop_handler.address == 0x001AEB7A);
    assert(noop_handler.kind == openaladdin::PlayerCollisionHandlerKind::NoOp);

    const auto unknown_handler = collision.player_collision_handler(0x55);
    assert(unknown_handler.address == 0);
    assert(unknown_handler.kind == openaladdin::PlayerCollisionHandlerKind::Unknown);

    std::vector<std::uint8_t> table_rom(0x1F82, 0);
    constexpr std::size_t handler_table_entry = 0x001CBE + 0x11 * 4;
    table_rom[handler_table_entry + 0] = 0x00;
    table_rom[handler_table_entry + 1] = 0x1A;
    table_rom[handler_table_entry + 2] = 0xF1;
    table_rom[handler_table_entry + 3] = 0x10;
    collision.bind_rom(table_rom);
    const auto table_handler = collision.player_collision_handler(0x11);
    assert(table_handler.address == 0x001AF110);
    assert(table_handler.kind
        == openaladdin::PlayerCollisionHandlerKind::PlayerLaunch);
    collision.bind_rom(rom);

    state.actors[2].type = 0x11;
    state.actors[2].frame_ptr = frame;
    state.actors[3].type = 0x0D;
    state.actors[3].frame_ptr = frame;
    state.actors[4].type = 0x80;
    state.actors[4].frame_ptr = frame;
    const auto detected = collision.detect_player_actor(
        state,
        openaladdin::PlayerCollisionInput{frame, false, false, false}
    );
    assert(detected.size() == 2);
    assert(detected[0].actor == 2);
    assert(detected[0].handler.kind
        == openaladdin::PlayerCollisionHandlerKind::PlayerLaunch);
    assert(detected[1].actor == 3);
    assert(detected[1].handler.kind == openaladdin::PlayerCollisionHandlerKind::NoOp);

    const auto classified_effects = collision.player_actor(
        state,
        openaladdin::PlayerCollisionInput{frame, false, false, false}
    );
    assert(classified_effects.unhandled_player_collision_types.size() == 1);
    assert(classified_effects.unhandled_player_collision_types.front() == 0x11);
    assert(state.actors[2].type == 0x11);
    assert(state.actors[3].type == 0x0D);
    assert(state.actors[4].type == 0x80);

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
