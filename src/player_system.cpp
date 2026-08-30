#include "player_system.hpp"

#include "actor_lifecycle.hpp"
#include "animation_system.hpp"
#include "interaction.hpp"

namespace openaladdin {

PlayerSystem::PlayerSystem(
    ActorLifecycleSystem& actor_lifecycle,
    AnimationSystem& animation,
    InteractionSystem& interactions
)
    : behavior_(TerrainBehaviorServices{
          actor_lifecycle,
          animation,
          interactions,
      }) {
}

void PlayerSystem::sample(GameState& state, const TerrainInput& input) const {
    terrain_.sample(state, input);
}

void PlayerSystem::apply_response(
    GameState& state,
    const TerrainResponseContext& context
) const {
    terrain_.apply_response(state, context);
}

bool PlayerSystem::advance_bounce_state(GameState& state) const {
    return terrain_.advance_bounce_state(state);
}

void PlayerSystem::apply_contour(
    GameState& state,
    const Level& level,
    bool& terrain_fall_phase
) const {
    terrain_.apply_contour(state, level, terrain_fall_phase);
}

std::optional<Level::TerrainCell> PlayerSystem::resolve(
    GameState& state,
    const Level& level,
    int previous_world_y,
    std::optional<std::uint8_t> behavior_override
) const {
    return terrain_.resolve(state, level, previous_world_y, behavior_override);
}

void PlayerSystem::apply_behavior(
    GameState& state,
    const Level::TerrainCell& cell,
    FrameRuntime& runtime,
    std::span<const std::uint8_t> rom
) const {
    behavior_.apply(state, cell, runtime, rom);
}

PlayerMotionResult PlayerSystem::update_horizontal(
    GameState& state,
    FrameRuntime& runtime,
    const InputState& input,
    const PlayerAnimationVm& animation,
    const PlayerMotionInput& context
) const {
    return motion_.update_horizontal(state, runtime, input, animation, context);
}

void PlayerSystem::integrate(GameState& state) const {
    motion_.integrate(state);
}

void PlayerSystem::finish_ground_release(
    GameState& state,
    FrameRuntime& runtime,
    int direction
) const {
    motion_.finish_ground_release(state, runtime, direction);
}

}  // namespace openaladdin
