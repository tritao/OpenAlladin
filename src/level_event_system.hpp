#pragma once

#include "actor_lifecycle.hpp"
#include "animation.hpp"
#include "level_event.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace openaladdin {

struct LevelEventEffects {
    // Audio remains a scheduler/host concern. The command interpreter returns
    // these requests so Engine can preserve its existing checkpointed queue.
    std::vector<std::uint8_t> sound_requests;
};

// Owns the semantic effects of the timed level-event command language. The
// LevelEventVm only frames records and decides when to dispatch them; this
// service owns actor creation, player presentation changes, and audio events.
class LevelEventSystem {
public:
    LevelEventSystem(
        ActorLifecycleSystem& actor_lifecycle,
        PlayerAnimationVm& animation,
        std::array<PlayerAnimationVm, 32>& actor_animations
    )
        : actor_lifecycle_(actor_lifecycle),
          animation_(animation),
          actor_animations_(actor_animations) {}

    LevelEventEffects dispatch(GameState& state, const LevelEventCommand& event);

private:
    bool spawn_actor(
        GameState& state,
        std::uint32_t template_address,
        std::uint16_t x,
        std::uint16_t y,
        std::uint32_t animation_override = 0,
        std::uint32_t movement_override = 0,
        bool override_type = false,
        std::uint8_t type = 0,
        bool override_movement_flags = false,
        std::uint8_t movement_flags = 0,
        bool override_sprite_attribute = false,
        std::uint16_t sprite_attribute = 0,
        bool set_facing_from_x = false
    );

    ActorLifecycleSystem& actor_lifecycle_;
    PlayerAnimationVm& animation_;
    std::array<PlayerAnimationVm, 32>& actor_animations_;
};

}  // namespace openaladdin
