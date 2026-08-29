#include "actor.hpp"

#include "checkpoint_io.hpp"

namespace openaladdin {

namespace {

void write_actor_state(checkpoint::Writer& writer, const ActorState& actor) {
    writer.u8(actor.type);
    writer.u16(actor.x);
    writer.u16(actor.y);
    writer.u8(actor.movement_flags);
    writer.u8(actor.runtime_field_07);
    writer.u8(actor.runtime_field_07_delay);
    writer.u8(actor.facing_x_flip);
    writer.u8(actor.facing_y_flip);
    writer.u32(actor.movement_pc);
    writer.u32(actor.movement_loop_pc);
    writer.u8(actor.movement_loop_timer);
    writer.i16(actor.movement_word_18);
    writer.i16(actor.movement_word_1a);
    writer.u16(actor.sprite_attribute);
    writer.u32(actor.frame_ptr);
    writer.u32(actor.animation_pc);
    writer.u32(actor.movement_return_pc);
    writer.u8(actor.flags);
    writer.u8(actor.interaction_state);
    writer.u8(actor.terminal_timer);
    writer.u8(actor.movement_command_timer);
    writer.u8(actor.animation_timer);
    writer.u8(actor.resource_count);
    writer.u16(actor.interaction_resource_offset);
    writer.u8(actor.interaction_selector);
    writer.i32(static_cast<std::int32_t>(actor.linked_actor_slot));
}

ActorState read_actor_state(checkpoint::Reader& reader) {
    ActorState actor;
    actor.type = reader.u8();
    actor.x = reader.u16();
    actor.y = reader.u16();
    actor.movement_flags = reader.u8();
    actor.runtime_field_07 = reader.u8();
    actor.runtime_field_07_delay = reader.u8();
    actor.facing_x_flip = reader.u8();
    actor.facing_y_flip = reader.u8();
    actor.movement_pc = reader.u32();
    actor.movement_loop_pc = reader.u32();
    actor.movement_loop_timer = reader.u8();
    actor.movement_word_18 = reader.i16();
    actor.movement_word_1a = reader.i16();
    actor.sprite_attribute = reader.u16();
    actor.frame_ptr = reader.u32();
    actor.animation_pc = reader.u32();
    actor.movement_return_pc = reader.u32();
    actor.flags = reader.u8();
    actor.interaction_state = reader.u8();
    actor.terminal_timer = reader.u8();
    actor.movement_command_timer = reader.u8();
    actor.animation_timer = reader.u8();
    actor.resource_count = reader.u8();
    actor.interaction_resource_offset = reader.u16();
    actor.interaction_selector = reader.u8();
    actor.linked_actor_slot = reader.i32();
    return actor;
}

void write_host_meta(checkpoint::Writer& writer, const ActorHostMeta& meta) {
    writer.boolean(meta.spawned_by_interaction);
    writer.boolean(meta.spawned_by_animation);
    writer.boolean(meta.spawned_by_apple);
}

ActorHostMeta read_host_meta(checkpoint::Reader& reader) {
    ActorHostMeta meta;
    meta.spawned_by_interaction = reader.boolean();
    meta.spawned_by_animation = reader.boolean();
    meta.spawned_by_apple = reader.boolean();
    return meta;
}

}  // namespace

void ActorSpriteResources::reset() {
    slot_bitmap_.fill(0);
}

std::optional<ActorResourceAllocation> ActorSpriteResources::allocate(
    std::uint8_t resource_count
) {
    const std::size_t requested_slots = static_cast<std::size_t>(resource_count) + 1;
    if (requested_slots > slot_bitmap_.size()) return std::nullopt;

    for (std::size_t first = 0; first + requested_slots <= slot_bitmap_.size(); ++first) {
        bool free = true;
        for (std::size_t slot = first; slot < first + requested_slots; ++slot) {
            if (slot_bitmap_[slot] != 0) {
                free = false;
                break;
            }
        }
        if (!free) continue;

        for (std::size_t slot = first; slot < first + requested_slots; ++slot) {
            slot_bitmap_[slot] = 0xFF;
        }
        return ActorResourceAllocation{
            static_cast<std::uint8_t>(first),
            static_cast<std::uint8_t>(requested_slots),
            kVramBaseStart + static_cast<std::uint32_t>(first * 0x80)
        };
    }
    return std::nullopt;
}

void ActorSpriteResources::release(const ActorResourceAllocation& allocation) {
    if (!allocation.valid()) return;
    const std::size_t first = allocation.first_slot;
    const std::size_t count = allocation.slot_count;
    if (first >= slot_bitmap_.size() || count > slot_bitmap_.size() - first) return;
    for (std::size_t slot = first; slot < first + count; ++slot) {
        slot_bitmap_[slot] = 0;
    }
}

void ActorSystem::begin_frame() {
    culled_this_frame_.fill(false);
}

void ActorSystem::reset(const Table& templates, bool snapshot_mode) {
    templates_ = templates;
    snapshot_mode_ = snapshot_mode;
    host_meta_.fill({});
    resource_allocations_.fill(std::nullopt);
    sprite_resources_.reset();
    if (snapshot_mode) {
        static_cast<Table&>(*this) = templates;
    } else {
        fill({});
    }
    begin_frame();
}

void ActorSystem::reset() {
    reset(templates_, snapshot_mode_);
}

std::optional<std::size_t> ActorSystem::allocate_actor_slot(ActorAllocationPool pool) const {
    const auto free_slot = [this](int slot) -> std::optional<std::size_t> {
        if (slot < 0 || slot >= static_cast<int>(size())) return std::nullopt;
        const auto index = static_cast<std::size_t>(slot);
        return (*this)[index].type == 0 && !culled_this_frame_[index]
            ? std::optional<std::size_t>(index)
            : std::nullopt;
    };

    int first = 0;
    int last = -1;
    int step = 1;
    switch (pool) {
    case ActorAllocationPool::CommonForward:
        first = 3; last = 22; break;
    case ActorAllocationPool::CommonReverse:
        first = 20; last = 1; step = -1; break;
    case ActorAllocationPool::GameplayForward:
        first = 1; last = 24; break;
    case ActorAllocationPool::GameplayReverse:
        first = 24; last = 1; step = -1; break;
    case ActorAllocationPool::AuxiliaryForward:
        first = 25; last = 30; break;
    }
    for (int slot = first; step > 0 ? slot <= last : slot >= last; slot += step) {
        if (auto found = free_slot(slot)) return found;
    }
    return std::nullopt;
}

std::optional<ActorResourceAllocation> ActorSystem::allocate_sprite_resources(
    ActorIndex slot,
    std::uint8_t resource_count
) {
    if (slot >= size()) return std::nullopt;
    release_sprite_resources(slot);
    const auto allocation = sprite_resources_.allocate(resource_count);
    if (!allocation) return std::nullopt;
    resource_allocations_[slot] = *allocation;
    return allocation;
}

void ActorSystem::release_sprite_resources(ActorIndex slot) {
    if (slot >= size()) return;
    if (resource_allocations_[slot]) {
        sprite_resources_.release(*resource_allocations_[slot]);
        resource_allocations_[slot].reset();
    }
}

std::optional<ActorResourceAllocation> ActorSystem::resource_allocation(ActorIndex slot) const {
    if (slot >= size()) return std::nullopt;
    return resource_allocations_[slot];
}

std::vector<std::size_t> ActorSystem::cull_interaction_actors(
    int left,
    int right,
    int top,
    int bottom
) {
    std::vector<std::size_t> culled;
    for (std::size_t slot = 1; slot < size(); ++slot) {
        ActorState& actor = (*this)[slot];
        if (!host_meta_[slot].spawned_by_interaction
            || actor.type == 0 || actor.terminal_timer != 0) {
            continue;
        }
        if (static_cast<int>(actor.x) < left || static_cast<int>(actor.x) > right
            || static_cast<int>(actor.y) < top || static_cast<int>(actor.y) > bottom) {
            culled_this_frame_[slot] = true;
            culled.push_back(slot);
        }
    }
    return culled;
}

bool ActorSystem::was_culled_this_frame(std::size_t slot) const {
    return slot < culled_this_frame_.size() && culled_this_frame_[slot];
}

void ActorSystem::write_checkpoint(std::ostream& output) const {
    checkpoint::Writer writer(output);
    writer.boolean(snapshot_mode_);
    for (std::size_t slot = 0; slot < templates_.size(); ++slot) {
        write_actor_state(writer, templates_[slot]);
        write_host_meta(writer, {});
    }
    for (std::size_t slot = 0; slot < size(); ++slot) {
        write_actor_state(writer, (*this)[slot]);
        write_host_meta(writer, host_meta_[slot]);
    }
    for (const bool culled : culled_this_frame_) writer.boolean(culled);
    for (const auto& allocation : resource_allocations_) {
        writer.boolean(allocation.has_value());
        if (allocation) {
            writer.u8(allocation->first_slot);
            writer.u8(allocation->slot_count);
            writer.u32(allocation->genesis_vram_base);
        }
    }
    for (const std::uint8_t occupied : sprite_resources_.slot_bitmap()) {
        writer.u8(occupied);
    }
}

void ActorSystem::read_checkpoint(std::istream& input) {
    checkpoint::Reader reader(input);
    const bool snapshot_mode = reader.boolean();
    Table templates{};
    Table records{};
    std::array<ActorHostMeta, 32> host_meta{};
    std::array<bool, 32> culled{};
    for (ActorState& actor : templates) {
        actor = read_actor_state(reader);
        (void)read_host_meta(reader);
    }
    for (std::size_t slot = 0; slot < records.size(); ++slot) {
        records[slot] = read_actor_state(reader);
        host_meta[slot] = read_host_meta(reader);
    }
    for (bool& value : culled) value = reader.boolean();
    std::array<std::optional<ActorResourceAllocation>, 32> allocations{};
    for (auto& allocation : allocations) {
        if (!reader.boolean()) continue;
        allocation = ActorResourceAllocation{
            reader.u8(),
            reader.u8(),
            reader.u32()
        };
    }
    std::array<std::uint8_t, ActorSpriteResources::kSlotCount> bitmap{};
    for (std::uint8_t& occupied : bitmap) occupied = reader.u8();
    templates_ = templates;
    static_cast<Table&>(*this) = records;
    host_meta_ = host_meta;
    culled_this_frame_ = culled;
    resource_allocations_ = allocations;
    sprite_resources_.restore_slot_bitmap(bitmap);
    snapshot_mode_ = snapshot_mode;
}

}  // namespace openaladdin
