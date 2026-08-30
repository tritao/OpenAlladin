#include "animation_system.hpp"

#include "game_state.hpp"

namespace openaladdin {

void AnimationSystem::bind_state(GameState& state) {
    player_.bind_state(state);
    actor_animation_.bind_state(state);
}

void AnimationSystem::load_rom(const std::string& path) {
    player_.load_rom(path);
    actor_animation_.load_rom(path);
}

void AnimationSystem::reset() {
    player_.reset();
    actor_animation_.reset();
}

AnimationContext AnimationSystem::player_context(GameState& state, bool grounded) const {
    AnimationContext context;
    context.state = &state;
    context.grounded_override = grounded;
    return context;
}

AnimationServices AnimationSystem::services(
    ActorIndex source_actor,
    bool defer_player_spawns,
    bool defer_mode3_spawns
) {
    return actor_animation_.services(
        source_actor,
        defer_player_spawns,
        defer_mode3_spawns
    );
}

void AnimationSystem::update_common(
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
) {
    AnimationServices services = this->services(0, false, true);
    if (response_dynamic_handoff) {
        player_.select_locomotion_entry(0x00121AD8, true);
    } else if (bounce_response_finished) {
        player_.select_locomotion_entry(
            0x00122006,
            true,
            SpritePose::Run
        );
    } else {
        player_.update(desired_pose, direction, context, frame_phase, &services);
    }
    actor_animation_.update(
        state,
        frame,
        frame_phase,
        context,
        observe_transition,
        observe_actor_flags
    );
}

void AnimationSystem::flush_deferred_spawn(
    GameState& state,
    int source_world_x,
    int source_world_y
) {
    F5Command command;
    if (!player_.take_deferred_spawn_command(command)) return;

    if (command.mode == 0) {
        // Player VM context is captured before movement integration, but the
        // ROM's player F5 allocator reads the live player record.
        command.source_world_x = source_world_x;
        command.source_world_y = source_world_y;
    }
    (void)actor_animation_.spawn_f5(0, command);
    if (command.apple_action) {
        state.player.animation_selector.state_lock = 0;
    }
}

std::vector<std::uint8_t> AnimationSystem::take_sound_requests() {
    std::vector<std::uint8_t> requests = player_.take_sound_requests();
    auto actor_requests = actor_animation_.take_sound_requests();
    requests.insert(requests.end(), actor_requests.begin(), actor_requests.end());
    return requests;
}

void AnimationSystem::set_writer_trace_enabled(bool enabled) {
    player_.set_writer_trace_enabled(enabled);
    actor_animation_.set_writer_trace_enabled(enabled);
}

void AnimationSystem::clear_writer_trace() {
    player_.clear_writer_trace();
    actor_animation_.clear_writer_trace();
}

void AnimationSystem::write_checkpoint(std::ostream& output) const {
    player_.write_checkpoint(output);
    actor_animation_.write_checkpoint(output);
}

void AnimationSystem::read_checkpoint(std::istream& input) {
    player_.read_checkpoint(input);
    actor_animation_.read_checkpoint(input);
}

}  // namespace openaladdin
