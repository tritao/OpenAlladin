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

    const auto fd = interaction.describe_spawn(0xFD);
    assert(fd);
    assert(fd->valid && fd->selector == 0xFD);
    assert(fd->template_address == 0x001B8354);
    assert(fd->allocation_pool == ActorAllocationPool::CommonReverse);
    assert(fd->post_offset_x == 0x14 && fd->post_offset_y == -1);
    assert(!fd->override_type);
    assert(!fd->override_animation);
    assert(!fd->override_movement);
    assert(!fd->override_resource_count);

    const auto fe = interaction.describe_spawn(0xFE);
    assert(fe);
    assert(fe->valid && fe->selector == 0xFE);
    assert(fe->template_address == fd->template_address);
    assert(fe->allocation_pool == ActorAllocationPool::CommonReverse);
    assert(fe->post_offset_x == 0x0B && fe->post_offset_y == 6);
    assert(!interaction.describe_spawn(0xFC));
    return 0;
}
