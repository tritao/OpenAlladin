#include "interaction.hpp"

#include <array>
#include <cassert>

int main() {
    using namespace openaladdin;

    ActorSystem actors;
    ActorLifecycleSystem lifecycle(actors);
    CollisionSystem collisions(lifecycle);
    AnimationSystem animation(lifecycle);
    std::array<bool, 32> movement_deferred{};
    InteractionSystem interaction(
        lifecycle,
        collisions,
        animation,
        movement_deferred
    );

    GameState state;
    state.interaction_state.target_current = 3;
    state.interaction_state.response_current = 1;
    state.frame.phase = 1;
    interaction.update_target(state);
    assert(state.interaction_state.target_current == 3);

    state.frame.phase = 2;
    interaction.update_target(state);
    assert(state.interaction_state.target_current == 2);
    state.frame.phase = 4;
    interaction.update_target(state);
    assert(state.interaction_state.target_current == 1);

    state.interaction_state.response_current = 1;
    state.interaction_state.response_pending = 3;
    interaction.advance_response_target(state);
    assert(state.interaction_state.response_current == 2);
    interaction.advance_response_target(state);
    assert(state.interaction_state.response_current == 3);
    interaction.advance_response_target(state);
    assert(state.interaction_state.response_current == 3);

    state.player.terrain_terminal_transition = 0;
    state.interaction_state.target_current = 7;
    const auto active = interaction.dispatch_target_state(state);
    assert(active.selector == 7);
    assert(!active.terminal_transition);
    assert(state.player.terrain_terminal_transition == 0);

    state.interaction_state.target_current = 0;
    const auto terminal = interaction.dispatch_target_state(state);
    assert(terminal.selector == 0);
    assert(terminal.terminal_transition);
    assert(state.player.terrain_terminal_transition == 0xFF);
    return 0;
}
