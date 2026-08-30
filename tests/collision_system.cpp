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

    // Type 0x40 is the checkpoint fixture for the apple pickup record. Its
    // strict secondary overlap increments the gameplay-owned counter before
    // replacing the collected actor.
    state.actors.fill({});
    state.player = {};
    state.camera = {};
    state.camera.x = 0;
    state.camera.y = 0;
    state.camera.vdp_update = 1;
    state.inventory.apple_count = 10;
    state.actors[1].type = 0x40;
    state.actors[1].frame_ptr = frame;
    const auto apple_pickup = collision.player_actor(
        state,
        openaladdin::PlayerCollisionInput{frame, false, false, false, false}
    );
    assert(apple_pickup.unhandled_player_collision_types.empty());
    assert(state.inventory.apple_count == 11);
    assert(state.actors[1].type == 0);
    assert(apple_pickup.sound_requests.size() == 1);
    assert(apple_pickup.sound_requests.front() == 0x0B);

    // The ROM suppresses the pickup cue when the VDP/audio update gate is
    // clear, while the inventory and actor replacement still occur.
    state.actors.fill({});
    state.player = {};
    state.camera = {};
    state.camera.x = 0;
    state.camera.y = 0;
    state.camera.vdp_update = 0;
    state.inventory.apple_count = 10;
    state.actors[1].type = 0x35;
    state.actors[1].frame_ptr = frame;
    const auto gated_apple_pickup = collision.player_actor(
        state,
        openaladdin::PlayerCollisionInput{frame, false, false, false, false}
    );
    assert(gated_apple_pickup.sound_requests.empty());
    assert(state.inventory.apple_count == 11);
    assert(state.actors[1].type == 0);

    const auto launch_handler = collision.player_collision_handler(0x11);
    assert(launch_handler.address == 0x001AF110);
    assert(launch_handler.kind
        == openaladdin::PlayerCollisionHandlerKind::PlayerLaunch);
    assert(launch_handler.native_implemented);

    const auto type3e_handler = collision.player_collision_handler(0x3E);
    assert(type3e_handler.address == 0x001AF2B0);
    assert(type3e_handler.kind == openaladdin::PlayerCollisionHandlerKind::ActorResponse);
    assert(type3e_handler.native_implemented);
    const auto type3f_handler = collision.player_collision_handler(0x3F);
    assert(type3f_handler.address == 0x001AF2FA);
    assert(type3f_handler.kind == openaladdin::PlayerCollisionHandlerKind::ActorResponse);
    assert(type3f_handler.native_implemented);
    const auto type65_handler = collision.player_collision_handler(0x65);
    assert(type65_handler.address == 0x001AFBF4);
    assert(type65_handler.kind == openaladdin::PlayerCollisionHandlerKind::ActorResponse);
    assert(type65_handler.native_implemented);
    const auto apple_handler = collision.player_collision_handler(0x35);
    assert(apple_handler.address == 0x001AF468);
    assert(apple_handler.native_implemented);
    const auto type4d_handler = collision.player_collision_handler(0x4D);
    assert(type4d_handler.address == 0x001AF0B8);
    assert(type4d_handler.native_implemented);

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
    assert(classified_effects.unhandled_player_collision_types.empty());
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

    // The ungated type-0x12 path selects the source actor's interaction
    // response and does not alter the player.
    state.actors.fill({});
    state.player = {};
    state.camera = {};
    state.camera.x = 0;
    state.camera.y = 0;
    state.actors[1].type = 0x12;
    state.actors[1].frame_ptr = frame;
    const auto ungated = collision.player_actor(
        state,
        openaladdin::PlayerCollisionInput{frame, false, false, false, false}
    );
    assert(ungated.unhandled_player_collision_types.empty());
    assert(state.actors[1].animation_pc == 0x0012512C);
    assert(state.actors[1].animation_timer == 0);
    assert(state.player.vx == 0 && state.player.vy == 0);

    // The gated type-0x11 response launches the player away from the source,
    // arms the terrain response, changes camera thresholds, and requests the
    // common player response animation/audio path.
    state.actors.fill({});
    state.player = {};
    state.camera = {};
    state.camera.x = 0;
    state.camera.y = 0;
    state.camera.vdp_update = 1;
    state.actors[1].type = 0x11;
    state.actors[1].frame_ptr = frame;
    const auto placement = collision.player_actor(
        state,
        openaladdin::PlayerCollisionInput{frame, false, false, false, true}
    );
    assert(placement.unhandled_player_collision_types.empty());
    assert(state.player.vx == -0x0400 && state.player.vy == -0x0400);
    assert(state.player.terrain_response_active == 0xFF);
    assert(state.player.terrain_vertical_stop == 0);
    assert(state.camera.horizontal_threshold == 0x00B0);
    assert(state.camera.vertical_threshold == 0x0160);
    assert(placement.player_animation_stream == 0x00121C62);
    assert(placement.sound_requests.size() == 1);
    assert(placement.sound_requests.front() == 0x31);

    // Type 0x4E uses the source facing bit for the launch direction and the
    // shared PRNG to choose one of its two recovered audio responses.
    state.actors.fill({});
    state.player = {};
    state.camera = {};
    state.camera.x = 0;
    state.camera.y = 0;
    state.camera.vdp_update = 1;
    state.random.value = 0;
    state.actors[1].type = 0x4E;
    state.actors[1].frame_ptr = frame;
    const auto type4e = collision.player_actor(
        state,
        openaladdin::PlayerCollisionInput{frame, false, false, false, false}
    );
    assert(type4e.unhandled_player_collision_types.empty());
    assert(state.player.vx == 0x0700 && state.player.vy == -0x0700);
    assert(state.player.terrain_response_active == 0xFF);
    assert(state.actors[1].type == 0x84);
    assert(state.actors[1].animation_pc == 0x00124B1A);
    assert(state.actors[1].animation_timer == 0);
    assert(type4e.player_animation_stream == 0x00121C62);
    assert(type4e.sound_requests.size() == 1);
    assert(type4e.sound_requests.front() == 0x47);

    // Type 0x4F has a strict 0x18-wide source/player window and launches
    // vertically while converting the source to its terminal response.
    state.actors.fill({});
    state.player = {};
    state.camera = {};
    state.camera.x = 0;
    state.camera.y = 0;
    state.camera.vdp_update = 1;
    state.actors[1].type = 0x4F;
    state.actors[1].frame_ptr = frame;
    state.player.x = 0x19;
    const auto outside_type4f_window = collision.dispatch_player_handler(
        state,
        openaladdin::PlayerActorCollision{
            1, collision.player_collision_handler(0x4F)},
        openaladdin::PlayerCollisionInput{frame, false, false, false, false}
    );
    assert(!outside_type4f_window.player_animation_stream.has_value());
    assert(state.actors[1].type == 0x4F);
    state.player.x = 0;
    const auto type4f = collision.player_actor(
        state,
        openaladdin::PlayerCollisionInput{frame, false, false, false, false}
    );
    assert(type4f.unhandled_player_collision_types.empty());
    assert(state.player.vy == -0x0800);
    assert(state.player.terrain_response_active == 0xFF);
    assert(state.actors[1].type == 0x84);
    assert(state.actors[1].animation_pc == 0x00124B3E);
    assert(state.actors[1].animation_timer == 0);
    assert(type4f.player_animation_stream == 0x00121C62);
    assert(type4f.sound_requests.size() == 1);
    assert(type4f.sound_requests.front() == 0x4B);

    // Types 0x3E and 0x3F share an in-place Type-0x84 response movement
    // stream. Each handler publishes its own interaction latch, updates the
    // camera thresholds, and gates sound 0x64 on the VDP-update byte.
    state.actors.fill({});
    state.player = {};
    state.camera = {};
    state.camera.vdp_update = 1;
    state.camera.horizontal_threshold = 0x0123;
    state.camera.vertical_threshold = 0x0456;
    state.interaction_state.type3e_response_latch = 0;
    state.interaction_state.type3f_response_latch = 0;
    state.actors[1].type = 0x3E;
    state.actors[1].frame_ptr = frame;
    state.actors[1].animation_timer = 9;
    state.actors[1].movement_pc = 0x00123456;
    state.actors[1].movement_word_18 = 0x1234;
    state.actors[1].movement_word_1a = 0x2345;
    const auto type3e = collision.dispatch_player_handler(
        state,
        openaladdin::PlayerActorCollision{
            1, collision.player_collision_handler(0x3E)},
        openaladdin::PlayerCollisionInput{frame, false, false, false, false}
    );
    assert(type3e.unhandled_player_collision_types.empty());
    assert(collision.player_collision_handler(0x3E).native_implemented);
    assert(state.interaction_state.type3e_response_latch == 0xFF);
    assert(state.interaction_state.type3f_response_latch == 0);
    assert(state.actors[1].type == 0x84);
    assert(state.actors[1].movement_pc == 0x00121618);
    assert(state.actors[1].animation_timer == 0);
    assert(state.actors[1].movement_word_18 == 0x1234);
    assert(state.actors[1].movement_word_1a == 0x2345);
    assert(state.camera.horizontal_threshold == 0x0070);
    assert(state.camera.vertical_threshold == 0x0190);
    assert(type3e.sound_requests.size() == 1);
    assert(type3e.sound_requests.front() == 0x64);

    state.actors.fill({});
    state.camera = {};
    state.camera.vdp_update = 0;
    state.interaction_state.type3e_response_latch = 0xFF;
    state.interaction_state.type3f_response_latch = 0;
    state.actors[1].type = 0x3F;
    state.actors[1].frame_ptr = frame;
    state.actors[1].animation_timer = 7;
    state.actors[1].movement_pc = 0x00654321;
    const auto type3f = collision.dispatch_player_handler(
        state,
        openaladdin::PlayerActorCollision{
            1, collision.player_collision_handler(0x3F)},
        openaladdin::PlayerCollisionInput{frame, false, false, false, false}
    );
    assert(type3f.unhandled_player_collision_types.empty());
    assert(collision.player_collision_handler(0x3F).native_implemented);
    assert(state.interaction_state.type3e_response_latch == 0xFF);
    assert(state.interaction_state.type3f_response_latch == 0xFF);
    assert(state.actors[1].type == 0x84);
    assert(state.actors[1].movement_pc == 0x00121618);
    assert(state.actors[1].animation_timer == 0);
    assert(state.camera.horizontal_threshold == 0x0070);
    assert(state.camera.vertical_threshold == 0x0190);
    assert(type3f.sound_requests.empty());

    // Type 0x65 is dispatched after player integration. The collision pass
    // therefore converts it through the dedicated post-motion entry while
    // leaving the ordinary pre-motion player scan free to continue.
    state.actors.fill({});
    state.player = {};
    state.camera = {};
    state.camera.x = 0;
    state.camera.y = 0;
    state.actors[1].type = 0x65;
    state.actors[1].frame_ptr = frame;
    state.player.vy = 0x0100;
    const auto bounce = collision.bounce_player_actor(
        state,
        openaladdin::PlayerCollisionInput{frame, false, false, false, false}
    );
    assert(bounce.unhandled_player_collision_types.empty());
    assert(bounce.player_bounce_response_started);
    assert(bounce.player_animation_stream == 0x001221B8);
    assert(bounce.player_animation_state_immediate);
    assert(state.actors[1].type == 0x66);
    assert(state.actors[1].animation_pc == 0x001244B0);
    assert(state.actors[1].animation_timer == 0);
    assert(state.player.y == -0x1F);
    assert(state.player.vy == -0x04C4);
    assert(state.player.terrain_response_active == 0xFF);
    assert(state.player.terrain_vertical_stop == 0);
    assert(state.player.terrain_response_timer_state == 0);
    assert(state.player.terrain_jump_response_counter == 1);
    assert(state.player.animation_selector.response_timer == 0);

    state.actors.fill({});
    state.player = {};
    state.camera = {};
    state.camera.x = 0;
    state.camera.y = 0;
    state.player.vy = 0x0100;
    state.player.animation_selector.animation_gate = 0xFF;
    state.actors[1].type = 0x65;
    state.actors[1].frame_ptr = frame;
    const auto gated_bounce = collision.bounce_player_actor(
        state,
        openaladdin::PlayerCollisionInput{frame, false, false, false, false}
    );
    assert(!gated_bounce.player_bounce_response_started);
    assert(state.actors[1].type == 0x65);

    // Type 0x4D uses the first Type-0x12 gameplay actor as its paired
    // response context. The pair receives the dedicated response animation,
    // while the source becomes the Type-0x84 response actor and nudges the
    // player horizontally according to the pair's world-X ordering.
    state.actors.fill({});
    state.player = {};
    state.camera = {};
    state.camera.x = 0;
    state.camera.y = 0;
    state.player.x = 15;
    state.player.vx = 0x0200;
    state.actors[1].type = 0x12;
    state.actors[1].x = 20;
    state.actors[1].frame_ptr = frame;
    state.actors[1].animation_timer = 7;
    state.actors[2].type = 0x4D;
    state.actors[2].x = 15;
    state.actors[2].frame_ptr = frame;
    state.actors[2].movement_pc = 0x00123456;
    state.actors[2].movement_word_18 = 0x1234;
    state.actors[2].movement_word_1a = 0x2345;
    const auto type4d = collision.dispatch_player_handler(
        state,
        openaladdin::PlayerActorCollision{
            2, collision.player_collision_handler(0x4D)},
        openaladdin::PlayerCollisionInput{frame, false, false, false, false}
    );
    assert(type4d.unhandled_player_collision_types.empty());
    assert(state.camera.horizontal_threshold == 0x00B0);
    assert(state.player.vx == 0x0300);
    assert(state.actors[1].animation_pc == 0x001222C2);
    assert(state.actors[1].animation_timer == 0);
    assert(state.actors[2].type == 0x84);
    assert(state.actors[2].animation_pc == 0x00124498);
    assert(state.actors[2].animation_timer == 0);
    assert(state.actors[2].movement_pc == 0x00123456);
    assert(state.actors[2].movement_word_18 == 0);
    assert(state.actors[2].movement_word_1a == 0);

    // The opposite ordering takes the subtract branch, and no paired actor
    // leaves the camera/VX response untouched while still converting source.
    state.actors[1].x = 10;
    state.actors[2].type = 0x4D;
    state.actors[2].animation_pc = 0;
    state.player.vx = 0x0200;
    state.camera.horizontal_threshold = 0x0123;
    const auto type4d_left = collision.dispatch_player_handler(
        state,
        openaladdin::PlayerActorCollision{
            2, collision.player_collision_handler(0x4D)},
        openaladdin::PlayerCollisionInput{frame, false, false, false, false}
    );
    assert(type4d_left.unhandled_player_collision_types.empty());
    assert(state.camera.horizontal_threshold == 0x00B0);
    assert(state.player.vx == 0x0100);
    assert(state.actors[2].type == 0x84);

    state.actors.fill({});
    state.player.vx = 0x0200;
    state.camera.horizontal_threshold = 0x0123;
    state.actors[2].type = 0x4D;
    state.actors[2].frame_ptr = frame;
    const auto type4d_without_pair = collision.dispatch_player_handler(
        state,
        openaladdin::PlayerActorCollision{
            2, collision.player_collision_handler(0x4D)},
        openaladdin::PlayerCollisionInput{frame, false, false, false, false}
    );
    assert(type4d_without_pair.unhandled_player_collision_types.empty());
    assert(state.camera.horizontal_threshold == 0x0123);
    assert(state.player.vx == 0x0200);
    assert(state.actors[2].type == 0x84);

    state.actors.fill({});
    state.player = {};
    state.camera = {};
    state.camera.x = 0;
    state.camera.y = 0;
    state.player.animation_selector.animation_gate = 0xFF;
    state.actors[1].type = 0x4F;
    state.actors[1].frame_ptr = frame;
    const auto gated_out = collision.player_actor(
        state,
        openaladdin::PlayerCollisionInput{frame, false, false, false, false}
    );
    assert(!gated_out.player_animation_stream.has_value());
    assert(state.actors[1].type == 0x4F);
    assert(state.player.vy == 0);
    return 0;
}
