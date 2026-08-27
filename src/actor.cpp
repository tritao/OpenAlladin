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
    writer.u32(actor.frame_ptr);
    writer.u32(actor.animation_pc);
    writer.u32(actor.movement_return_pc);
    writer.u8(actor.flags);
    writer.u8(actor.interaction_state);
    writer.u8(actor.terminal_timer);
    writer.u8(actor.movement_command_timer);
    writer.u8(actor.animation_timer);
    writer.u8(actor.animation_defer_ticks);
    writer.boolean(actor.animation_force_next_tick);
    writer.u8(actor.animation_tick_phase);
    writer.u8(actor.resource_count);
    writer.u16(actor.interaction_resource_offset);
    writer.u8(actor.interaction_selector);
    writer.boolean(actor.spawned_by_interaction);
    writer.boolean(actor.spawned_by_animation);
    writer.boolean(actor.spawned_by_apple);
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
    actor.frame_ptr = reader.u32();
    actor.animation_pc = reader.u32();
    actor.movement_return_pc = reader.u32();
    actor.flags = reader.u8();
    actor.interaction_state = reader.u8();
    actor.terminal_timer = reader.u8();
    actor.movement_command_timer = reader.u8();
    actor.animation_timer = reader.u8();
    actor.animation_defer_ticks = reader.u8();
    actor.animation_force_next_tick = reader.boolean();
    actor.animation_tick_phase = reader.u8();
    actor.resource_count = reader.u8();
    actor.interaction_resource_offset = reader.u16();
    actor.interaction_selector = reader.u8();
    actor.spawned_by_interaction = reader.boolean();
    actor.spawned_by_animation = reader.boolean();
    actor.spawned_by_apple = reader.boolean();
    actor.linked_actor_slot = reader.i32();
    return actor;
}

}  // namespace

void ActorSystem::begin_frame() {
    culled_this_frame_.fill(false);
}

void ActorSystem::reset(const Table& templates, bool snapshot_mode) {
    templates_ = templates;
    snapshot_mode_ = snapshot_mode;
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

std::vector<std::size_t> ActorSystem::cull_interaction_actors(
    int left,
    int right,
    int top,
    int bottom
) {
    std::vector<std::size_t> culled;
    for (std::size_t slot = 1; slot < size(); ++slot) {
        ActorState& actor = (*this)[slot];
        if (!actor.spawned_by_interaction || actor.type == 0 || actor.terminal_timer != 0) {
            continue;
        }
        if (static_cast<int>(actor.x) < left || static_cast<int>(actor.x) > right
            || static_cast<int>(actor.y) < top || static_cast<int>(actor.y) > bottom) {
            culled_this_frame_[slot] = true;
            actor.type = 0;
            actor.spawned_by_interaction = false;
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
    for (const ActorState& actor : templates_) write_actor_state(writer, actor);
    for (const ActorState& actor : *this) write_actor_state(writer, actor);
    for (const bool culled : culled_this_frame_) writer.boolean(culled);
}

void ActorSystem::read_checkpoint(std::istream& input) {
    checkpoint::Reader reader(input);
    const bool snapshot_mode = reader.boolean();
    Table templates{};
    Table records{};
    std::array<bool, 32> culled{};
    for (ActorState& actor : templates) actor = read_actor_state(reader);
    for (ActorState& actor : records) actor = read_actor_state(reader);
    for (bool& value : culled) value = reader.boolean();
    templates_ = templates;
    static_cast<Table&>(*this) = records;
    culled_this_frame_ = culled;
    snapshot_mode_ = snapshot_mode;
}

}  // namespace openaladdin
