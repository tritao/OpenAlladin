#include "actor_lifecycle.hpp"
#include "animation_system.hpp"
#include "collision.hpp"
#include "interaction.hpp"
#include "player_system.hpp"

#include <cassert>

int main() {
    using namespace openaladdin;

    ActorSystem actors;
    ActorLifecycleSystem actor_lifecycle(actors);
    CollisionSystem collisions(actor_lifecycle);
    AnimationSystem animation(actor_lifecycle);
    std::array<bool, 32> actor_movement_deferred{};
    InteractionSystem interactions(
        actor_lifecycle,
        collisions,
        animation,
        actor_movement_deferred);
    PlayerSystem player(actor_lifecycle, animation, interactions);

    GameState state;
    player.sample(state, TerrainInput{true, false, false, true, true});
    assert(state.player.terrain_query_result == 0x56);
    assert(state.player.terrain_push_up == 0xFF);
    assert(state.player.terrain_push_right == 0xFF);
    assert(state.player.terrain_push_down == 0);

    state.player.terrain_query_state_a = 0xFF;
    state.player.terrain_stop_upward_motion = 0;
    const int initial_y = state.player.y;
    player.apply_response(state, TerrainResponseContext{0, false, false});
    assert(state.player.y == initial_y - 1);
    assert(state.player.terrain_transition_gate == 0xFF);
    assert(state.camera.vertical_threshold == 400);

    state.player.terrain_query_state_a = 0xFF;
    state.player.terrain_query_state_b = 0xFF;
    state.player.terrain_response_latch = 0xFF;
    state.player.terrain_state = 0xFF;
    Level level;
    assert(!player.resolve(state, level, state.camera.y));
    assert(state.player.terrain_query_state_a == 0);
    assert(state.player.terrain_query_state_b == 0);
    assert(state.player.terrain_response_latch == 0);
    assert(state.player.terrain_state == 0);

    state.player.terrain_response_active = 0;
    state.player.terrain_vertical_stop = 0;
    state.player.terrain_transition_gate = 0;
    state.player.animation_selector.transition_lock = 0;
    state.player.animation_selector.transition_state = 0;
    state.player.vy = 0x0100;
    state.player.terrain_bounce_animation_state = 0x27;
    assert(player.advance_bounce_state(state));
    assert(state.player.vy == 0x0178);
    assert(state.player.terrain_bounce_animation_state == 0x28);

    state.player.vy = 0x07F0;
    state.player.terrain_bounce_animation_state = 0;
    assert(!player.advance_bounce_state(state));
    assert(state.player.vy == 0x07F0);
    assert(state.player.terrain_bounce_animation_state == 1);

    state.player.terrain_response_active = 0xFF;
    state.player.terrain_vertical_stop = 0xFF;
    state.player.vy = 0x0100;
    const auto blocked_velocity = state.player.vy;
    assert(!player.advance_bounce_state(state));
    assert(state.player.vy == blocked_velocity);

    return 0;
}
