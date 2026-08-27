#include "actor.hpp"

namespace openaladdin {

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

}  // namespace openaladdin
