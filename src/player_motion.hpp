#pragma once

#include "game_state.hpp"

namespace openaladdin {

struct FrameRuntime;
struct InputState;
class PlayerAnimationVm;

struct PlayerMotionInput {
    bool was_grounded = false;
    bool contour_ground_motion = false;
    bool terrain_response_was_active = false;
};

struct PlayerMotionResult {
    bool ground_release = false;
    int ground_release_direction = 0;
};

// Owns local player movement after terrain has published its response state.
// The scheduler still decides when these operations run; this service owns
// the movement rules and their fixed-point arithmetic.
class PlayerMotionSystem {
public:
    PlayerMotionResult update_horizontal(
        GameState&,
        FrameRuntime&,
        const InputState&,
        const PlayerAnimationVm&,
        const PlayerMotionInput&
    ) const;

    void integrate(GameState&) const;

    void finish_ground_release(GameState&, FrameRuntime&, int direction) const;
};

}  // namespace openaladdin
