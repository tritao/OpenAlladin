#pragma once

#include "player_motion.hpp"
#include "player_terrain.hpp"
#include "terrain_behavior.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace openaladdin {

class ActorLifecycleSystem;
class AnimationSystem;
class InteractionSystem;

// The player gameplay boundary owns terrain sampling/resolution, terrain
// handler effects, and local motion. The existing implementation types are
// retained as private delegates for this first consolidation so their tested
// behavior and frame ordering remain unchanged while Engine exposes one
// player owner.
class PlayerSystem {
public:
    PlayerSystem(
        ActorLifecycleSystem& actor_lifecycle,
        AnimationSystem& animation,
        InteractionSystem& interactions
    );

    void sample(GameState&, const TerrainInput&) const;
    void apply_response(GameState&, const TerrainResponseContext&) const;
    bool advance_bounce_state(GameState&) const;
    void apply_contour(GameState&, const Level&, bool& terrain_fall_phase) const;
    std::optional<Level::TerrainCell> resolve(
        GameState&,
        const Level&,
        int previous_world_y,
        std::optional<std::uint8_t> behavior_override = std::nullopt
    ) const;

    void apply_behavior(
        GameState&,
        const Level::TerrainCell&,
        FrameRuntime&,
        std::span<const std::uint8_t>
    ) const;

    PlayerMotionResult update_horizontal(
        GameState&,
        FrameRuntime&,
        const InputState&,
        const PlayerAnimationVm&,
        const PlayerMotionInput& context
    ) const;
    void integrate(GameState&) const;
    void finish_ground_release(GameState&, FrameRuntime&, int direction) const;

private:
    PlayerTerrainSystem terrain_;
    PlayerMotionSystem motion_;
    TerrainBehaviorSystem behavior_;
};

}  // namespace openaladdin
