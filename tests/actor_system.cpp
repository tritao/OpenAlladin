#include "actor_lifecycle.hpp"

#include <cassert>

int main() {
    openaladdin::ActorSystem actors;
    actors[3].type = 1;
    actors.host_meta(3).spawned_by_interaction = true;
    actors[3].x = 0;
    actors[3].y = 0;
    actors.begin_frame();

    const auto common = actors.allocate_actor_slot(
        openaladdin::ActorAllocationPool::CommonForward);
    assert(common && *common == 4);

    const auto culled = actors.cull_interaction_actors(10, 20, 10, 20);
    assert(culled.size() == 1 && culled[0] == 3);
    assert(actors[3].type == 1);
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

    constexpr std::uint32_t kTemplate = 0x20;
    std::vector<std::uint8_t> rom(0x80, 0);
    rom[kTemplate + 0x00] = 0x42;
    rom[kTemplate + 0x02] = 0x40;
    rom[kTemplate + 0x03] = 0x12;
    rom[kTemplate + 0x04] = 0x34;
    rom[kTemplate + 0x05] = 0xFF;
    rom[kTemplate + 0x06] = 0x00;
    rom[kTemplate + 0x07] = 0x00;
    rom[kTemplate + 0x08] = 0x02;
    rom[kTemplate + 0x09] = 0x00;
    rom[kTemplate + 0x0A] = 0x12;
    rom[kTemplate + 0x0B] = 0x34;
    rom[kTemplate + 0x0C] = 0x00;
    rom[kTemplate + 0x0D] = 0x00;
    rom[kTemplate + 0x0E] = 0x03;
    rom[kTemplate + 0x0F] = 0x00;
    rom[kTemplate + 0x10] = 2;
    rom[kTemplate + 0x11] = 1;
    rom[kTemplate + 0x12] = 0x20;

    openaladdin::ActorSystem lifecycle_actors;
    openaladdin::ActorLifecycleSystem lifecycle(lifecycle_actors);
    lifecycle.bind_rom(rom);
    openaladdin::ActorState destination;
    destination.x = 100;
    destination.y = 200;
    destination.movement_loop_pc = 0x11111111;
    destination.movement_loop_timer = 7;
    destination.movement_return_pc = 0x22222222;
    destination.movement_word_18 = 0x1234;
    destination.movement_word_1a = 0x2345;
    destination.frame_ptr = 0x33333333;
    const auto initialized = lifecycle.initialize_record(destination, kTemplate);
    assert(initialized.type == 0x42);
    assert(initialized.x == 100 && initialized.y == 200);
    assert(initialized.movement_flags == 0x40);
    assert(initialized.runtime_field_07 == 0x12);
    assert(initialized.runtime_field_07_delay == 0x34);
    assert(initialized.facing_x_flip == 0xFF && initialized.facing_y_flip == 1);
    assert(initialized.movement_pc == 0x200);
    assert(initialized.sprite_attribute == 0x1234);
    assert(initialized.animation_pc == 0x300);
    assert(initialized.resource_count == 2);
    assert(initialized.flags == 0x20);
    assert(initialized.movement_loop_pc == 0x11111111);
    assert(initialized.movement_loop_timer == 7);
    assert(initialized.movement_return_pc == 0x22222222);
    assert(initialized.movement_word_18 == 0);
    assert(initialized.movement_word_1a == 0);
    assert(initialized.frame_ptr == 0);
    assert(initialized.linked_actor_slot == -1);
    assert(lifecycle.install(3, initialized));
    const auto allocation = lifecycle_actors.resource_allocation(3);
    assert(allocation && allocation->first_slot == 0
        && allocation->slot_count == 3
        && allocation->genesis_vram_base == 0x8600);
    lifecycle.release_resources(3);
    assert(!lifecycle_actors.resource_allocation(3));

    openaladdin::ActorSystem linked_actors;
    openaladdin::ActorLifecycleSystem linked_lifecycle(linked_actors);
    linked_lifecycle.bind_rom(rom);
    linked_actors[31].type = 0x55;
    linked_actors[31].flags = 0x80;
    openaladdin::F5Command linked_command;
    linked_command.valid = true;
    linked_command.mode = 5;
    linked_command.template_address = kTemplate;
    linked_command.source_world_x = 400;
    linked_command.source_world_y = 500;
    const auto child = linked_lifecycle.spawn_f5(31, linked_command);
    assert(child && *child == 1);
    assert(linked_actors[31].linked_actor_slot == 1);
    assert(linked_actors[31].flags == 0x80);
    assert(linked_actors[1].linked_actor_slot == 31);
    assert((linked_actors[1].flags & 0x04) != 0);
    assert(linked_actors[1].x == 400 && linked_actors[1].y == 500);
    assert(linked_actors.resource_allocation(1));
    linked_lifecycle.retire_from_vm(1, 0);
    assert(linked_actors[1].type == 0);
    assert(linked_actors[31].type == 0);
    assert(!linked_actors.resource_allocation(1));

    openaladdin::ActorSystem preserved_actors;
    openaladdin::ActorLifecycleSystem preserved_lifecycle(preserved_actors);
    preserved_lifecycle.bind_rom(rom);
    openaladdin::ActorState preserved_source;
    preserved_source.type = 0x22;
    preserved_source.resource_count = 0;
    assert(preserved_lifecycle.install(2, preserved_source));
    openaladdin::ActorState retiring_source;
    retiring_source.type = 0x11;
    retiring_source.flags = 0;
    retiring_source.linked_actor_slot = 2;
    assert(preserved_lifecycle.install(1, retiring_source));
    preserved_actors[2].linked_actor_slot = 1;
    preserved_actors[2].flags = 0x04;
    preserved_lifecycle.retire_from_vm(1, 0);
    assert(preserved_actors[1].type == 0);
    assert(preserved_actors[2].type == 0x22);
    assert(preserved_actors[2].linked_actor_slot == -1);
    assert((preserved_actors[2].flags & 0x04) == 0);
    assert(preserved_actors.resource_allocation(2));

    openaladdin::ActorSpriteResources resources;
    const auto large = resources.allocate(0x72);
    const auto last = resources.allocate(0);
    assert(large && large->first_slot == 0 && large->slot_count == 0x73);
    assert(last && last->first_slot == 0x73 && last->slot_count == 1);
    assert(!resources.allocate(0));
    resources.release(*large);
    assert(resources.allocate(0)->first_slot == 0);
    return 0;
}
