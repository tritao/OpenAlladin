#pragma once

#include "actor_lifecycle.hpp"
#include "game_state.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
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
    // FFF0D8 gates the shared placement/launch handler family. It is kept as
    // an explicit scheduler input until that interaction byte has a typed
    // GameState owner of its own.
    bool interaction_gate = false;
};

// The player-collision table is a semantic dispatch table, not just an
// implementation detail of the geometry pass. Keep the recovered families
// visible so callers and parity tooling can distinguish an intentional no-op
// from a handler that has been classified but not yet implemented natively.
enum class PlayerCollisionHandlerKind {
    Unknown,
    NoOp,
    SharedActorResponse,
    TerrainPushResponse,
    InteractionResponse,
    ActorResponse,
    PlayerPlacement,
    PlayerLaunch,
    TransitionResponse,
};

struct PlayerCollisionHandlerInfo {
    std::uint8_t actor_type = 0;
    std::uint32_t address = 0;
    PlayerCollisionHandlerKind kind = PlayerCollisionHandlerKind::Unknown;
    bool native_implemented = false;
};

struct PlayerActorCollision {
    ActorIndex actor = 0;
    PlayerCollisionHandlerInfo handler{};
};

struct CollisionEffects {
    bool player_collision_interaction_pending = false;
    bool player_bounce_response_started = false;
    bool player_damage_taken = false;
    // Collision handlers publish animation/audio work to their scheduler
    // owners; CollisionSystem does not reach into either VM or sound driver.
    std::optional<std::uint32_t> player_animation_stream;
    // Some recovered handlers write the player animation PC and timer
    // directly rather than selecting a new action stream.
    bool player_animation_state_immediate = false;
    bool player_idle_animation_reset = false;
    std::vector<std::uint8_t> sound_requests;
    // These are diagnostic parity edges, not Genesis state. They make a
    // colliding table entry visible until its recovered behavior is attached
    // to the corresponding native service.
    std::vector<std::uint8_t> unhandled_player_collision_types;
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

    PlayerCollisionHandlerInfo player_collision_handler(
        std::uint8_t actor_type
    ) const;
    std::vector<PlayerActorCollision> detect_player_actor(
        const GameState& state,
        const PlayerCollisionInput& input
    ) const;
    CollisionEffects dispatch_player_handler(
        GameState& state,
        const PlayerActorCollision& collision,
        const PlayerCollisionInput& input
    );

    CollisionEffects player_actor(
        GameState& state,
        const PlayerCollisionInput& input
    );
    // Type 0x65 is dispatched after player motion integration in the ROM
    // frame loop, so it has a dedicated post-motion collision boundary.
    CollisionEffects bounce_player_actor(
        GameState& state,
        const PlayerCollisionInput& input
    );
    void actor_actor(GameState& state);

private:
    bool is_opening_fire_actor(const ActorState& actor) const;
    bool opening_fire_contacts_player(
        const GameState& state,
        const PlayerCollisionInput& input,
        const ActorState& actor
    ) const;
    CollisionEffects detect_fire_damage(
        GameState& state,
        const PlayerCollisionInput& input
    ) const;
    bool valid_frame(std::uint32_t frame_pointer) const;
    void terminalize(
        GameState& state,
        ActorIndex slot,
        std::uint32_t animation_stream,
        std::uint8_t frames
    );
    void reinitialize_from_collision_template(GameState&, ActorIndex);

    ActorLifecycleSystem& actor_lifecycle_;
    const std::vector<std::uint8_t>* rom_ = nullptr;
};

}  // namespace openaladdin
