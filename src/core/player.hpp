#pragma once

#include "core/frame.hpp"

#include <cstdint>
#include <string_view>

namespace openaladdin::core {

// Input is a transition argument, not gameplay state. The core consumes it
// for one frame and publishes the ROM's resulting latches into GenesisRam.
struct CoreInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool jump_held = false;
    bool jump_pressed = false;
    bool attack_held = false;
    bool attack_pressed = false;
    bool apple_held = false;
    bool apple_pressed = false;
};

CoreInput core_input_from_token(std::string_view token);

// Recovered input/terrain-query publication. These functions only mutate
// their documented RAM locations; they do not retain a host-side PlayerState.
void player_sample_input(CoreRuntime& core, const CoreInput& input);

// The two player selectors publish ROM stream cursors and timers directly to
// actor zero. They intentionally use RAM latches for their predicates; no
// native pose or action-timer object is retained.
void player_select_locomotion_or_action(CoreRuntime& core, const CoreInput& input);
void player_select_action_animation(CoreRuntime& core, const CoreInput& input);

// Player_HandleJumpAndVerticalState at 0x001A9716. This owns the jump edge
// and the short active-response impulse window around the integrator.
void player_handle_jump_and_vertical_state(
    CoreRuntime& core,
    const CoreInput& input
);

// Player_IntegrateMotion at 0x001A9B90. Velocities are signed 8.8 values in
// the actor-0 record; the local coordinates and response latches are the
// separate RAM fields used by the ROM routine.
void player_integrate_motion(CoreRuntime& core);

}  // namespace openaladdin::core
