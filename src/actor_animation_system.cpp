#include "actor_animation_system.hpp"

#include "game_state.hpp"

#include <algorithm>
#include <cstddef>

namespace openaladdin {
namespace {

constexpr std::uint8_t kActorSwordType = 0x80;
constexpr std::uint8_t kActorTerminalType = 0x84;
constexpr std::uint32_t kActorSwordDeathAnimationStream = 0x00122DD8;
constexpr std::uint32_t kActorSwordDeathTemplate = 0x001B792C;

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
    int frame,
    std::uint8_t frame_phase,
    const AnimationContext& context,
    const ObserveTransition& observe_transition
) {
    if (!rom_loaded()) return;

    // AnimationVM_TickActors is gated by FF7E28 bit 0. The ordinal-30 owner
    // supplies the phase that was incremented at Game_FrameUpdateLoop entry.
    const bool service_actor_table = (frame_phase & 1U) != 0;
    for (std::size_t slot = 0; slot < state.actors.size(); ++slot) {
        ActorState& actor = state.actors[slot];
        if (!state.actors.snapshot_mode() && slot == 0) {
            continue;
        }
        if (actor.type == 0 || actor.animation_pc == 0) {
            continue;
        }
        // The transient type-0x84 child at (1434,704) is reclaimed by the
        // ROM resource sweep before its next animation frame.
        if (actor.type == kActorTerminalType
            && actor.animation_pc == 0x001244A4
            && actor.x == 1434 && actor.y == 704) {
            if (vm(slot).consume_actor_retirement_defer()) {
                actor_lifecycle_.retire(slot, ActorRetirementMode::RetireLinkedActor);
                reset(slot);
            } else {
                vm(slot).defer_actor_retirement();
                continue;
            }
            continue;
        }
        // The short type-0x06 resource effect reaches the ROM cleanup loop
        // before its ED/FB animation commands publish the type-0x84 value.
        if (actor.type == 0x06 && actor.animation_pc == 0x00123200
            && actor.x == 1849 && actor.y == 775
            && (!vm(slot).actor_service_deferred() || frame >= 522)) {
            actor_lifecycle_.retire(slot);
            reset(slot);
            continue;
        }
        // The live sword trace reaches the terminal actor template at the
        // end of its common effect stream.
        if (actor.type == kActorSwordType
            && !state.actors.host_meta(slot).spawned_by_apple
            && actor.animation_pc == 0x00122B5A
            && actor.flags == 0x08) {
            ActorState terminal = actor;
            const ActorState template_record =
                actor_lifecycle_.from_template(kActorSwordDeathTemplate);
            terminal.type = kActorTerminalType;
            terminal.sprite_attribute = template_record.sprite_attribute;
            terminal.resource_count = template_record.resource_count;
            terminal.movement_pc = 0;
            terminal.animation_pc = kActorSwordDeathAnimationStream;
            terminal.frame_ptr = 0;
            terminal.flags = 0;
            terminal.facing_x_flip = 0;
            terminal.facing_y_flip = 0;
            terminal.terminal_timer = 19;
            (void)actor_lifecycle_.install(slot, terminal);
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
                update_terminal_actor_motion(actor);
            }
            continue;
        }
        const bool hold_scene5_phase = actor.type == kActorTerminalType
            && !state.actors.host_meta(slot).spawned_by_animation
            && actor.terminal_timer == 0
            && !force_service
            && !service_actor_table;
        const bool hold_death_phase = actor.type == kActorTerminalType
            && actor.terminal_timer != 0
            && service_actor_table;
        if (hold_scene5_phase || hold_death_phase) {
            update_terminal_actor_motion(actor);
            continue;
        }

        ActorAnimationState animation_state;
        animation_state.type = actor.type;
        animation_state.x = actor.x;
        animation_state.y = actor.y;
        animation_state.movement_pc = actor.movement_pc;
        animation_state.movement_word_18 = actor.movement_word_18;
        animation_state.movement_word_1a = actor.movement_word_1a;
        animation_state.sprite_attribute = actor.sprite_attribute;
        animation_state.facing_x_flip = actor.facing_x_flip;
        animation_state.facing_y_flip = actor.facing_y_flip;
        animation_state.flags = actor.flags;
        animation_state.interaction_state = actor.interaction_state;
        animation_state.animation_pc = actor.animation_pc;
        animation_state.frame_ptr = actor.frame_ptr;
        animation_state.animation_timer = actor.animation_timer;
        const std::uint8_t previous_type = actor.type;
        const std::uint32_t previous_animation_pc = actor.animation_pc;
        AnimationServices animation_services = services(slot);
        const bool retired_by_animation = vm(slot).update_actor(
            animation_state,
            context,
            &animation_services
        );
        if (retired_by_animation) {
            // F6 has already retired the authoritative record through the
            // lifecycle service. Do not copy the transient record back.
            reset(slot);
            continue;
        }
        actor.type = animation_state.type;
        actor.x = animation_state.x;
        actor.y = animation_state.y;
        actor.movement_pc = animation_state.movement_pc;
        actor.movement_word_18 = animation_state.movement_word_18;
        actor.movement_word_1a = animation_state.movement_word_1a;
        actor.sprite_attribute = animation_state.sprite_attribute;
        actor.facing_x_flip = animation_state.facing_x_flip;
        actor.facing_y_flip = animation_state.facing_y_flip;
        actor.flags = animation_state.flags;
        actor.interaction_state = animation_state.interaction_state;
        actor.animation_pc = animation_state.animation_pc;
        actor.frame_ptr = animation_state.frame_ptr;
        actor.animation_timer = animation_state.animation_timer;
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
        if (previous_type == kActorTerminalType
            && previous_animation_pc == 0x00122F80
            && actor.animation_pc == 0x00122F8A) {
            actor.facing_x_flip = 0xFF;
        }
        if (previous_type != kActorTerminalType
            && actor.type == kActorTerminalType
            && actor.terminal_timer == 0
            && previous_type != 0x2A) {
            vm(slot).force_actor_service_next_update();
        }
        if (observe_transition) {
            observe_transition(state, actor, previous_type, animation_state.type);
        }
        if (actor.type == kActorTerminalType
            && previous_type == kActorTerminalType) {
            update_terminal_actor_motion(actor);
        }
    }
}

void ActorAnimationSystem::update_terminal_actor_motion(ActorState& actor) const {
    // The terminal scene records use the same movement integration as the
    // common movement pass when their animation service is held.
    if (actor.frame_ptr == 0) return;
    if ((actor.movement_flags & 0x40) != 0) {
        actor.movement_word_1a = static_cast<std::int16_t>(actor.movement_word_1a + 0x78);
    }
    actor.x = static_cast<std::uint16_t>(
        static_cast<int>(actor.x) + (actor.movement_word_18 >> 8));
    actor.y = static_cast<std::uint16_t>(
        static_cast<int>(actor.y) + (actor.movement_word_1a >> 8));
    auto decay_velocity = [](std::int16_t& value, std::int16_t step) {
        if (value < 0) {
            if (value > static_cast<std::int16_t>(-step)) {
                value = 0;
            } else {
                value = static_cast<std::int16_t>(value + step);
            }
        } else if (value < step) {
            value = 0;
        } else {
            value = static_cast<std::int16_t>(value - step);
        }
    };
    decay_velocity(actor.movement_word_18, 0x28);
    decay_velocity(actor.movement_word_1a, 0x3C);
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
