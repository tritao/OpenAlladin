#pragma once

#include "level.hpp"

#include <cstdint>
#include <span>

namespace openaladdin {

struct FrameRuntime;
struct GameState;
class ActorLifecycleSystem;
class AnimationSystem;
class InteractionSystem;

// Dependencies used by terrain handlers for effects that belong to other
// semantic owners. The handler dispatcher itself owns neither actor records
// nor animation/interaction state.
struct TerrainBehaviorServices {
    ActorLifecycleSystem& actors;
    AnimationSystem& animation;
    InteractionSystem& interactions;
};

// Owns the side effects of the recovered terrain-handler table. PlayerTerrain-
// System resolves the current cell and publishes terrain state; this class
// interprets the handler selected by that cell.
class TerrainBehaviorSystem {
public:
    explicit TerrainBehaviorSystem(TerrainBehaviorServices services)
        : services_(services) {}

    void apply(
        GameState& state,
        const Level::TerrainCell& cell,
        FrameRuntime& runtime,
        std::span<const std::uint8_t> rom
    ) const;

private:
    TerrainBehaviorServices services_;
};

}  // namespace openaladdin
