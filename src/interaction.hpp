#pragma once

#include "animation.hpp"
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
        PlayerAnimationVm& animation,
        std::array<PlayerAnimationVm, 32>& actor_animations,
        std::array<bool, 32>& actor_movement_deferred
    );

    void bind_rom(const std::vector<std::uint8_t>& rom) { rom_ = &rom; }

    void reset();
    void reset_scan();
    void clear_surface_interaction_state();

    InteractionFrameBoundary begin_frame(GameState& state);

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

    void update_actor_flags(GameState& state, bool stable_fixture);
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
    bool finish_bounce_response(GameState& state, bool terrain_response_was_active);
    void hold_bounce_camera_delay(GameState& state, bool bounce_response_finished);
    void bounce_actor_interaction(GameState& state, bool& terrain_fall_phase);

    void scan_refill_window(GameState& state, const Level& level, bool stable_fixture);
    void flush_surface_actor_spawn(GameState& state);

    const InteractionRuntimeState& runtime() const { return runtime_; }
    void restore_runtime(const InteractionRuntimeState& runtime) { runtime_ = runtime; }

private:
    std::optional<SpawnDescriptor> spawn_descriptor(std::uint8_t selector) const;
    void dispatch_interaction(
        GameState& state,
        const Level::InteractionRecord& record,
        int base_x,
        int base_y
    );

    ActorLifecycleSystem& actor_lifecycle_;
    CollisionSystem& collisions_;
    PlayerAnimationVm& animation_;
    std::array<PlayerAnimationVm, 32>& actor_animations_;
    std::array<bool, 32>& actor_movement_deferred_;
    const std::vector<std::uint8_t>* rom_ = nullptr;
    InteractionRuntimeState runtime_{};
};

}  // namespace openaladdin
