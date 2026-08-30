#pragma once

#include "actor_lifecycle.hpp"
#include "game_ram.hpp"
#include "movement.hpp"

#include <span>

namespace openaladdin {

struct GameState;
struct FrameRuntime;
class AnimationSystem;

// Owns the runtime boundary around the shared movement interpreter. The VM
// decodes movement streams; this service supplies the authoritative actor
// table, lifecycle callbacks, and frame scheduling gates.
class ActorMovementSystem {
public:
    ActorMovementSystem(
        MovementVm& vm,
        ActorLifecycleSystem& lifecycle,
        AnimationSystem& animation
    )
        : vm_(vm), lifecycle_(lifecycle), animation_(animation) {}

    void update(
        GameState& state,
        FrameRuntime& runtime,
        std::span<const std::uint8_t> rom
    );

private:
    MovementVm& vm_;
    ActorLifecycleSystem& lifecycle_;
    AnimationSystem& animation_;
    GameRamView ram_;
};

}  // namespace openaladdin
