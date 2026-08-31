#include "actor_lifecycle.hpp"

namespace openaladdin {

std::uint8_t ActorLifecycleSystem::read8(std::uint32_t address) const {
    return rom_ != nullptr && address < rom_->size() ? (*rom_)[address] : 0;
}

std::uint16_t ActorLifecycleSystem::read16(std::uint32_t address) const {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(read8(address)) << 8) | read8(address + 1));
}

std::uint32_t ActorLifecycleSystem::read32(std::uint32_t address) const {
    return (static_cast<std::uint32_t>(read8(address)) << 24)
        | (static_cast<std::uint32_t>(read8(address + 1)) << 16)
        | (static_cast<std::uint32_t>(read8(address + 2)) << 8)
        | read8(address + 3);
}

std::optional<ActorIndex> ActorLifecycleSystem::allocate(ActorPool pool) const {
    return actors_.allocate_actor_slot(pool);
}

ActorState ActorLifecycleSystem::from_template(std::uint32_t template_address) const {
    ActorState actor;
    if (rom_ == nullptr || template_address + 0x12 >= rom_->size()) return actor;

    // Actor_InitializeFromTemplate (0x001AE30A) consumes the compact 20-byte
    // record with the following field map. The remaining destination fields
    // are intentionally untouched by initialize_record().
    actor.type = read8(template_address + 0x00);
    actor.actor_timer = read8(template_address + 0x01);
    actor.movement_flags = read8(template_address + 0x02);
    actor.runtime_field_07 = read8(template_address + 0x03);
    actor.runtime_field_07_delay = read8(template_address + 0x04);
    actor.facing_x_flip = read8(template_address + 0x05);
    actor.movement_pc = read32(template_address + 0x06);
    actor.sprite_attribute = read16(template_address + 0x0A);
    actor.animation_pc = read32(template_address + 0x0C);
    actor.resource_count = read8(template_address + 0x10);
    actor.facing_y_flip = read8(template_address + 0x11);
    actor.flags = read8(template_address + 0x12);
    return actor;
}

ActorState ActorLifecycleSystem::initialize_record(
    const ActorState& destination,
    std::uint32_t template_address
) const {
    ActorState actor = from_template(template_address);
    // The ROM deliberately leaves coordinates and the caller-owned movement
    // continuation fields untouched during template initialization.
    actor.x = destination.x;
    actor.y = destination.y;
    actor.movement_loop_pc = destination.movement_loop_pc;
    actor.movement_loop_timer = destination.movement_loop_timer;
    actor.movement_return_pc = destination.movement_return_pc;
    return actor;
}

void ActorLifecycleSystem::clear_record(ActorIndex actor) {
    if (actor >= actors_.size()) return;
    actors_.release_sprite_resources(actor);
    actors_[actor].type = 0;
    actors_[actor].linked_actor_slot = -1;
    actors_.host_meta(actor) = {};
}

void ActorLifecycleSystem::release_resources(ActorIndex actor) {
    actors_.release_sprite_resources(actor);
}

bool ActorLifecycleSystem::install(ActorIndex destination, const ActorState& record) {
    if (destination >= actors_.size()) return false;

    actors_.release_sprite_resources(destination);
    actors_.host_meta(destination) = {};
    actors_[destination] = record;
    if (record.type == 0) return true;

    if (actors_.allocate_sprite_resources(destination, record.resource_count)) {
        return true;
    }

    // Actor_AllocateSpriteVRAM publishes and clears the current actor when no
    // contiguous run is available. A linked record is part of that same
    // failure boundary.
    const int linked = actors_[destination].linked_actor_slot;
    clear_record(destination);
    if (linked >= 0 && static_cast<ActorIndex>(linked) < actors_.size()) {
        clear_record(static_cast<ActorIndex>(linked));
    }
    return false;
}

std::optional<ActorIndex> ActorLifecycleSystem::spawn_f5(
    ActorIndex source,
    const F5Command& command
) {
    if (!command.valid || rom_ == nullptr
        || command.template_address + 0x12 >= rom_->size()) {
        return std::nullopt;
    }
    if (command.mode > 6) return std::nullopt;
    if ((command.mode == 5 || command.mode == 6) && source >= actors_.size()) {
        return std::nullopt;
    }

    int offset_x = command.offset_x;
    int offset_y = command.offset_y;
    if (command.source_facing_x_flip != 0) offset_x = -offset_x;
    if (command.source_facing_y_flip != 0) offset_y = -offset_y;

    if (command.mode == 4) {
        // Mode 4 is the special in-place branch and does not consume another
        // actor slot. It still runs the common template/resource initializer.
        if (source >= actors_.size()) return std::nullopt;
        ActorState spawned = initialize_record(actors_[source], command.template_address);
        spawned.facing_x_flip = command.source_facing_x_flip;
        spawned.facing_y_flip = command.source_facing_y_flip;
        if (command.animation_override != 0) spawned.animation_pc = command.animation_override;
        if (command.movement_override != 0) spawned.movement_pc = command.movement_override;
        spawned.x = static_cast<std::uint16_t>(command.source_world_x + offset_x);
        spawned.y = static_cast<std::uint16_t>(command.source_world_y + offset_y);
        if (!install(source, spawned)) return std::nullopt;
        actors_.host_meta(source).spawned_by_animation = true;
        actors_.host_meta(source).spawned_by_apple = command.apple_action;
        return source;
    }

    ActorPool pool = ActorPool::CommonForward;
    switch (command.mode) {
    case 1: pool = ActorPool::GameplayForward; break;
    case 2: pool = ActorPool::CommonReverse; break;
    case 3: pool = ActorPool::AuxiliaryForward; break;
    case 5: pool = ActorPool::GameplayForward; break;
    case 6: pool = ActorPool::CommonReverse; break;
    default: break;
    }
    const auto destination = allocate(pool);
    if (!destination) return std::nullopt;

    ActorState spawned = initialize_record(actors_[*destination], command.template_address);
    spawned.facing_x_flip = command.source_facing_x_flip;
    spawned.facing_y_flip = command.source_facing_y_flip;
    if (command.animation_override != 0) spawned.animation_pc = command.animation_override;
    if (command.movement_override != 0) spawned.movement_pc = command.movement_override;
    spawned.x = static_cast<std::uint16_t>(command.source_world_x + offset_x);
    spawned.y = static_cast<std::uint16_t>(command.source_world_y + offset_y);
    if (command.mode == 5 || command.mode == 6) {
        spawned.linked_actor_slot = static_cast<int>(source);
    }
    if (!install(*destination, spawned)) return std::nullopt;

    ActorHostMeta& meta = actors_.host_meta(*destination);
    meta.spawned_by_animation = true;
    meta.spawned_by_apple = command.apple_action;
    if (command.mode == 5 || command.mode == 6) {
        // The common handler publishes the reciprocal link and marks the
        // child record with the linked-resource flag. The source and child
        // both retain the reciprocal +0x3E link.
        actors_[*destination].flags = static_cast<std::uint8_t>(
            actors_[*destination].flags | 0x04U);
        actors_[source].linked_actor_slot = static_cast<int>(*destination);
    }
    return destination;
}

void ActorLifecycleSystem::retire(
    ActorIndex actor,
    ActorRetirementMode mode
) {
    if (actor >= actors_.size()) return;
    const int linked = actors_[actor].linked_actor_slot;
    clear_record(actor);

    if (mode != ActorRetirementMode::RetireLinkedActor
        || linked < 0 || static_cast<ActorIndex>(linked) >= actors_.size()) {
        return;
    }
    clear_record(static_cast<ActorIndex>(linked));
}

void ActorLifecycleSystem::retire_from_vm(ActorIndex actor, std::uint8_t command_mode) {
    if (actor >= actors_.size()) return;
    const bool linked_flag = (actors_[actor].flags & 0x04U) != 0;
    const ActorRetirementMode mode = command_mode == 0 && !linked_flag
        ? ActorRetirementMode::PreserveLinkedActor
        : ActorRetirementMode::RetireLinkedActor;
    if (mode == ActorRetirementMode::PreserveLinkedActor) {
        const int linked = actors_[actor].linked_actor_slot;
        clear_record(actor);
        if (linked >= 0 && static_cast<ActorIndex>(linked) < actors_.size()) {
            ActorState& linked_actor = actors_[static_cast<ActorIndex>(linked)];
            linked_actor.linked_actor_slot = -1;
            linked_actor.flags = static_cast<std::uint8_t>(linked_actor.flags & ~0x04U);
        }
        return;
    }
    retire(actor, ActorRetirementMode::RetireLinkedActor);
}

}  // namespace openaladdin
