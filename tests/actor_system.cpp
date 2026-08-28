#include "actor.hpp"

#include <cassert>

int main() {
    openaladdin::ActorSystem actors;
    actors[3].type = 1;
    actors[3].spawned_by_interaction = true;
    actors[3].x = 0;
    actors[3].y = 0;
    actors.begin_frame();

    const auto common = actors.allocate_actor_slot(
        openaladdin::ActorAllocationPool::CommonForward);
    assert(common && *common == 4);

    const auto culled = actors.cull_interaction_actors(10, 20, 10, 20);
    assert(culled.size() == 1 && culled[0] == 3);
    assert(actors[3].type == 0);
    assert(actors.was_culled_this_frame(3));
    const auto after_cull = actors.allocate_actor_slot(
        openaladdin::ActorAllocationPool::CommonForward);
    assert(after_cull && *after_cull != 3);

    openaladdin::ActorSystem pools;
    for (auto& actor : pools) actor.type = 1;
    pools[3].type = 0;
    pools[22].type = 0;
    pools[1].type = 0;
    pools[24].type = 0;
    pools[20].type = 0;
    pools[25].type = 0;
    pools[30].type = 0;
    pools.begin_frame();
    assert(pools.allocate_actor_slot(
        openaladdin::ActorAllocationPool::CommonForward) == std::optional<std::size_t>(3));
    assert(pools.allocate_actor_slot(
        openaladdin::ActorAllocationPool::CommonReverse) == std::optional<std::size_t>(20));
    assert(pools.allocate_actor_slot(
        openaladdin::ActorAllocationPool::GameplayForward) == std::optional<std::size_t>(1));
    assert(pools.allocate_actor_slot(
        openaladdin::ActorAllocationPool::GameplayReverse) == std::optional<std::size_t>(24));
    assert(pools.allocate_actor_slot(
        openaladdin::ActorAllocationPool::AuxiliaryForward) == std::optional<std::size_t>(25));

    openaladdin::ActorSystem::Table templates{};
    templates[1].type = 0x2D;
    actors.set_snapshot_mode(true);
    actors.templates() = templates;
    actors.reset();
    assert(actors[1].type == 0x2D);
    return 0;
}
