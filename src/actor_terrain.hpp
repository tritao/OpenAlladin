#pragma once

#include <cstdint>
#include <span>

namespace openaladdin {

class ActorLifecycleSystem;
class AnimationSystem;
class Level;
struct GameState;

// Owns the actor-side terrain pass that runs after MovementVM. The pass is
// deliberately separate from PlayerTerrainSystem: it samples actor records,
// converts terrain-triggered records through the lifecycle service, and
// publishes the actor animation deferrals observed at the recovered ROM
// boundary.
class ActorTerrainSystem {
public:
    ActorTerrainSystem(
        ActorLifecycleSystem& lifecycle,
        AnimationSystem& animation
    ) : lifecycle_(lifecycle), animation_(animation) {}

    void update(
        GameState& state,
        const Level& level,
        std::span<const std::uint8_t> rom,
        bool stable_fixture
    );

private:
    ActorLifecycleSystem& lifecycle_;
    AnimationSystem& animation_;
};

}  // namespace openaladdin
