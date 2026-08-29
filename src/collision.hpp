#pragma once

#include "actor_lifecycle.hpp"
#include "game_state.hpp"

#include <cstdint>
#include <vector>

namespace openaladdin {

// Bounds decoded from a Genesis animation frame. The display path and the
// player/actor hitbox path intentionally have different facing arithmetic;
// both are kept here so callers cannot accidentally substitute one for the
// other.
struct CollisionBox {
    bool valid = false;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct PlayerCollisionInput {
    std::uint32_t frame_pointer = 0;
    bool facing_left = false;
    bool sword_active = false;
    bool bounce_response_follow_active = false;
};

struct CollisionEffects {
    bool player_collision_interaction_pending = false;
};

// Owns the recovered actor collision geometry and actor dispatch scans. The
// native scheduler still decides when these passes run; this class owns what
// a pass means and delegates actor lifetime changes to ActorLifecycleSystem.
class CollisionSystem {
public:
    explicit CollisionSystem(ActorLifecycleSystem& actor_lifecycle)
        : actor_lifecycle_(actor_lifecycle) {}

    void bind_rom(const std::vector<std::uint8_t>& rom) { rom_ = &rom; }

    CollisionBox frame_bounds(
        std::uint32_t frame_pointer,
        int origin_x,
        int origin_y,
        bool facing_left
    ) const;
    CollisionBox hitbox(
        std::uint32_t frame_pointer,
        int origin_x,
        int origin_y,
        bool facing_left
    ) const;

    static bool overlaps(const CollisionBox& first, const CollisionBox& second);
    static bool strict_overlaps(const CollisionBox& first, const CollisionBox& second);

    bool player_actor_overlap(
        const GameState& state,
        std::uint32_t player_frame_pointer,
        bool player_facing_left,
        ActorIndex actor
    ) const;
    bool any_player_actor_overlap(
        const GameState& state,
        std::uint32_t player_frame_pointer,
        bool player_facing_left,
        std::uint8_t actor_type,
        ActorIndex first_slot,
        ActorIndex last_slot
    ) const;

    CollisionEffects player_actor(
        GameState& state,
        const PlayerCollisionInput& input
    );
    void actor_actor(GameState& state);

private:
    bool valid_frame(std::uint32_t frame_pointer) const;
    void terminalize(
        GameState& state,
        ActorIndex slot,
        std::uint32_t animation_stream,
        std::uint8_t frames
    );

    ActorLifecycleSystem& actor_lifecycle_;
    const std::vector<std::uint8_t>* rom_ = nullptr;
};

}  // namespace openaladdin
