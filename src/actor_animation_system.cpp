#include "actor_animation_system.hpp"

#include "game_state.hpp"
#include "movement.hpp"

#include <algorithm>
#include <cstddef>

namespace openaladdin {
namespace {

constexpr std::uint8_t kActorTerminalType = 0x84;
constexpr std::uint32_t kActorSwordDeathAnimationStream = 0x00122DD8;

}  // namespace

void ActorAnimationSystem::bind_state(GameState& state) {
    state_ = &state;
    for (auto& vm : vms_) {
        vm.bind_state(state);
    }
}

void ActorAnimationSystem::load_rom(const std::string& path) {
    for (auto& vm : vms_) {
        vm.load_rom(path);
    }
}

void ActorAnimationSystem::reset() {
    for (auto& vm : vms_) {
        vm.reset();
    }
}

void ActorAnimationSystem::reset(ActorIndex slot) {
    vms_.at(slot).reset();
}

AnimationServices ActorAnimationSystem::services(
    ActorIndex source_actor,
    bool defer_player_spawns,
    bool defer_mode3_spawns
) {
    AnimationServices services;
    services.source_actor = source_actor;
    services.defer_player_spawns = defer_player_spawns;
    services.defer_mode3_spawns = defer_mode3_spawns;
    services.spawn_f5 = [this](ActorIndex source, const F5Command& command) {
        return spawn_f5(source, command);
    };
    services.retire_actor = [this](ActorIndex actor, std::uint8_t command_mode) {
        actor_lifecycle_.retire_from_vm(actor, command_mode);
    };
    return services;
}

std::optional<ActorIndex> ActorAnimationSystem::spawn_f5(
    ActorIndex source_actor,
    const F5Command& command
) {
    if (state_ == nullptr) return std::nullopt;
    const auto slot = actor_lifecycle_.spawn_f5(source_actor, command);
    if (!slot) return std::nullopt;

    reset(*slot);
    if (command.apple_action) {
        // The allocated projectile reaches the common actor table on the
        // current boundary, but its first frame is consumed on the next one.
        vm(*slot).defer_actor_service();
    }
    return slot;
}

void ActorAnimationSystem::update(
    GameState& state,
    std::uint8_t frame_phase,
    const AnimationContext& context,
    const ObserveTransition& observe_transition,
    const ObserveActorFlags& observe_actor_flags
) {
    if (!rom_loaded()) return;

    // AnimationVM_TickActors is gated by FF7E28 bit 0. The ordinal-30 owner
    // supplies the phase that was incremented at Game_FrameUpdateLoop entry.
    const bool service_actor_table = (frame_phase & 1U) != 0;
    for (std::size_t slot = 0; slot < state.actors.size(); ++slot) {
        ActorState& actor = state.actors[slot];
        // Slot 0 is the player record. The player VM is serviced by the
        // scheduler's ordinal-30 pass below; ticking its mirrored actor VM a
        // second time would consume action streams twice and can fire an F5
        // child spawn on the input frame.
        if (slot == 0) {
            continue;
        }
        if (actor.type == 0 || actor.animation_pc == 0) {
            continue;
        }
        // The apple child has its own every-other-VBlank cadence beginning at
        // allocation; unlike the shared table, it is serviced on both phases.
        const bool apple_actor_service = state.actors.host_meta(slot).spawned_by_apple;
        const bool force_service = vm(slot).actor_service_forced();
        const bool actor_service = vm(slot).consume_actor_service(
            service_actor_table || apple_actor_service,
            service_actor_table
        );
        if (!actor_service) {
            if (!service_actor_table
                && !apple_actor_service && actor.type == kActorTerminalType
                && actor.terminal_timer == 0) {
                MovementVm::integrate_actor(actor);
            }
            continue;
        }
        const bool hold_scene5_phase = actor.type == kActorTerminalType
            && !state.actors.host_meta(slot).spawned_by_animation
            && actor.terminal_timer == 0
            && !force_service
            && !service_actor_table;
        const bool sword_death_stream = actor.type == kActorTerminalType
            && actor.animation_pc >= kActorSwordDeathAnimationStream
            && actor.animation_pc < 0x00122E20;
        const bool generated_sword_death = sword_death_stream
            && state.actors.host_meta(slot).spawned_by_animation;
        const bool hold_death_phase = actor.type == kActorTerminalType
            && actor.terminal_timer != 0
            && service_actor_table
            && !generated_sword_death;
        if (hold_scene5_phase || hold_death_phase) {
            MovementVm::integrate_actor(actor);
            continue;
        }

        const std::uint8_t previous_type = actor.type;
        const std::uint8_t previous_flags = actor.flags;
        AnimationServices animation_services = services(slot);
        const bool retired_by_animation = vm(slot).update_actor(
            actor,
            context,
            &animation_services
        );
        if (retired_by_animation) {
            // F6 has already retired the authoritative record through the
            // lifecycle service. Do not copy the transient record back.
            reset(slot);
            continue;
        }
        if (observe_actor_flags
            && ((previous_flags ^ actor.flags) & 0x20U) != 0) {
            observe_actor_flags(state, actor, previous_flags);
        }
        if (previous_type != 0 && actor.type == 0
            && actor.linked_actor_slot >= 0
            && static_cast<std::size_t>(actor.linked_actor_slot) < state.actors.size()) {
            state.actors[static_cast<std::size_t>(actor.linked_actor_slot)].flags =
                static_cast<std::uint8_t>(
                    state.actors[static_cast<std::size_t>(actor.linked_actor_slot)].flags
                    & ~0x04U
                );
            actor.linked_actor_slot = -1;
        }
        if (observe_transition) {
            observe_transition(state, actor, previous_type, actor.type);
        }
        if (actor.type == kActorTerminalType
            && previous_type == kActorTerminalType) {
            MovementVm::integrate_actor(actor);
        }
    }
}

std::vector<std::uint8_t> ActorAnimationSystem::take_sound_requests() {
    std::vector<std::uint8_t> requests;
    for (auto& vm : vms_) {
        auto vm_requests = vm.take_sound_requests();
        requests.insert(requests.end(), vm_requests.begin(), vm_requests.end());
    }
    return requests;
}

void ActorAnimationSystem::set_writer_trace_enabled(bool enabled) {
    for (auto& vm : vms_) {
        vm.set_writer_trace_enabled(enabled);
    }
}

void ActorAnimationSystem::clear_writer_trace() {
    for (auto& vm : vms_) {
        vm.clear_writer_trace();
    }
}

std::vector<std::uint32_t> ActorAnimationSystem::writer_pcs() const {
    std::vector<std::uint32_t> pcs;
    for (const auto& vm : vms_) {
        const auto& vm_pcs = vm.writer_pcs();
        pcs.insert(pcs.end(), vm_pcs.begin(), vm_pcs.end());
    }
    return pcs;
}

void ActorAnimationSystem::write_checkpoint(std::ostream& output) const {
    for (const PlayerAnimationVm& vm : vms_) {
        vm.write_checkpoint(output);
    }
}

void ActorAnimationSystem::read_checkpoint(std::istream& input) {
    for (PlayerAnimationVm& vm : vms_) {
        vm.read_checkpoint(input);
    }
}

}  // namespace openaladdin
