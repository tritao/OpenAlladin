#pragma once

#include "animation_system.hpp"
#include "collision.hpp"
#include "game_state.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace openaladdin {

struct InteractionFrameBoundary {
    bool selector_pending_at_start = false;
    bool arm_surface_interaction = false;
};

struct InteractionTargetDispatch {
    std::uint8_t selector = 0;
    bool terminal_transition = false;
};

// Runtime edges owned by the interaction scheduler. These are not another
// copy of Genesis RAM: they describe deferred calls whose effects become
// visible at a later recovered frame boundary.
struct InteractionRuntimeState {
    bool scan_initialized = false;
    bool selector_pending = false;
    bool actor_lock_pending = false;
    bool camera_delay_pending = false;
    bool actor_triggered = false;
    bool player_collision_pending = false;
    bool surface_actor_spawn_pending = false;
    int surface_actor_spawn_x = 0;
    int surface_actor_spawn_y = 0;
    bool surface_interaction_pending = false;
    bool surface_interaction_active = false;
    bool bounce_response_active = false;
    bool bounce_response_follow_active = false;
    bool bounce_camera_delay_hold_pending = false;
    int reference_x = 0;
    int reference_y = 0;
};

// Owns the interaction refill window and the deferred player/actor handoffs
// that surround the common animation service. Actor records remain in
// GameState; lifetime changes go through ActorLifecycleSystem.
class InteractionSystem {
public:
    InteractionSystem(
        ActorLifecycleSystem& actor_lifecycle,
        CollisionSystem& collisions,
        AnimationSystem& animation_system,
        std::array<bool, 32>& actor_movement_deferred
    );

    void bind_rom(const std::vector<std::uint8_t>& rom) { rom_ = &rom; }

    void reset();
    void reset_scan();
    void clear_surface_interaction_state();

    InteractionFrameBoundary begin_frame(GameState& state);

    // These operations model the small global interaction state helpers. The
    // response bytes live in GameState; only deferred frame-boundary edges
    // remain in InteractionRuntimeState.
    void advance_response_target(GameState& state) const;
    void update_target(GameState& state) const;
    InteractionTargetDispatch dispatch_target_state(GameState& state) const;

    void request_surface_actor_spawn(int world_x, int world_y);
    void apply_surface_terrain_behavior(GameState& state);
    void apply_surface_interaction_lock(GameState& state, bool arm);
    void arm_player_selector(bool camera_delay);
    void start_bounce_response() { runtime_.bounce_response_active = true; }
    bool surface_interaction_active() const {
        return runtime_.surface_interaction_active;
    }
    bool bounce_response_active() const { return runtime_.bounce_response_active; }
    bool bounce_response_follow_active() const {
        return runtime_.bounce_response_follow_active;
    }

    void consume_collision_effects(const CollisionEffects& effects);
    bool player_collision_pending() const {
        return runtime_.player_collision_pending;
    }
    void apply_player_collision_selector(GameState& state, bool stable_fixture);

    void observe_actor_flag_transition(
        GameState& state,
        const ActorState& actor,
        std::uint8_t previous_flags
    );
    void observe_surface_actor_transition(
        GameState& state,
        const ActorState& actor,
        std::uint8_t previous_type,
        std::uint8_t published_type
    );
    void process_surface_actor_collision(
        GameState& state,
        AnimationContext context,
        bool stable_fixture,
        bool contour_ground_motion
    );
    void update_player_selector(
        GameState& state,
        AnimationContext context,
        bool stable_fixture,
        bool landing_event,
        SpritePose desired_pose,
        bool contour_ground_motion,
        bool selector_pending_at_start
    );

    void clear_response_handoff();
    bool finish_bounce_response(
        GameState& state,
        bool terrain_response_was_active,
        bool jump_response_was_complete
    );
    void hold_bounce_camera_delay(GameState& state, bool bounce_response_finished);

    void scan_refill_window(GameState& state, const Level& level, bool stable_fixture);
    void flush_surface_actor_spawn(GameState& state);

    // Returns the ROM-backed actor initialization contract for a confirmed
    // interaction selector. The descriptor is data-only; dispatch still
    // performs allocation, template initialization, and resource ownership.
    std::optional<SpawnDescriptor> describe_spawn(std::uint8_t selector) const;

    const InteractionRuntimeState& runtime() const { return runtime_; }
    void restore_runtime(const InteractionRuntimeState& runtime) { runtime_ = runtime; }

private:
    void dispatch_interaction(
        GameState& state,
        const Level::InteractionRecord& record,
        int base_x,
        int base_y
    );

    ActorLifecycleSystem& actor_lifecycle_;
    CollisionSystem& collisions_;
    AnimationSystem& animation_system_;
    std::array<bool, 32>& actor_movement_deferred_;
    const std::vector<std::uint8_t>* rom_ = nullptr;
    InteractionRuntimeState runtime_{};
};

}  // namespace openaladdin
