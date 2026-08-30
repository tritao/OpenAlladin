#pragma once

#include "actor_animation_system.hpp"

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace openaladdin {

// Owns the player-facing VM and the shared actor-table animation service.
// The VMs remain interpreters; Genesis-observable records live in GameState.
class AnimationSystem {
public:
    using ObserveTransition = ActorAnimationSystem::ObserveTransition;
    using ObserveActorFlags = ActorAnimationSystem::ObserveActorFlags;

    explicit AnimationSystem(ActorLifecycleSystem& actor_lifecycle)
        : actor_animation_(actor_lifecycle) {}

    void bind_state(GameState& state);
    void load_rom(const std::string& path);
    void reset();

    PlayerAnimationVm& player() { return player_; }
    const PlayerAnimationVm& player() const { return player_; }
    ActorAnimationSystem& actors() { return actor_animation_; }
    const ActorAnimationSystem& actors() const { return actor_animation_; }

    AnimationContext player_context(GameState& state, bool grounded) const;
    AnimationServices services(
        ActorIndex source_actor,
        bool defer_player_spawns = false,
        bool defer_mode3_spawns = false
    );

    void update_common(
        GameState& state,
        int frame,
        std::uint8_t frame_phase,
        SpritePose desired_pose,
        HorizontalDirection direction,
        const AnimationContext& context,
        bool response_dynamic_handoff,
        bool bounce_response_finished,
        const ObserveTransition& observe_transition,
        const ObserveActorFlags& observe_actor_flags
    );

    void flush_deferred_spawn(GameState& state, int source_world_x, int source_world_y);
    std::vector<std::uint8_t> take_sound_requests();
    void set_writer_trace_enabled(bool enabled);
    void clear_writer_trace();
    void write_checkpoint(std::ostream& output) const;
    void read_checkpoint(std::istream& input);

private:
    PlayerAnimationVm player_;
    ActorAnimationSystem actor_animation_;
};

}  // namespace openaladdin
