#include "core/actor.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

void write_u16(std::vector<std::uint8_t>& rom, std::size_t offset,
               std::uint16_t value) {
    rom[offset] = static_cast<std::uint8_t>(value >> 8);
    rom[offset + 1] = static_cast<std::uint8_t>(value);
}

void write_u32(std::vector<std::uint8_t>& rom, std::size_t offset,
               std::uint32_t value) {
    rom[offset] = static_cast<std::uint8_t>(value >> 24);
    rom[offset + 1] = static_cast<std::uint8_t>(value >> 16);
    rom[offset + 2] = static_cast<std::uint8_t>(value >> 8);
    rom[offset + 3] = static_cast<std::uint8_t>(value);
}

}  // namespace

int main() {
    using namespace openaladdin::core;

    constexpr std::size_t kTemplate = 0x20;
    constexpr std::size_t kRomSize = kActorSpriteVramBaseTable + 0x80;
    std::vector<std::uint8_t> rom(kRomSize, 0);
    rom[kTemplate] = 0x42;
    rom[kTemplate + 1] = 0x07;
    rom[kTemplate + 2] = 0x40;
    rom[kTemplate + 3] = 0x12;
    rom[kTemplate + 4] = 0x34;
    rom[kTemplate + 5] = 0xFF;
    write_u32(rom, kTemplate + 0x06, 0x0012146C);
    write_u16(rom, kTemplate + 0x0A, 0x6000);
    write_u32(rom, kTemplate + 0x0C, 0x0012437E);
    rom[kTemplate + 0x10] = 2;
    rom[kTemplate + 0x11] = 1;
    rom[kTemplate + 0x12] = 0x20;
    write_u32(rom, kActorSpriteVramBaseTable, 0x00008600);

    CoreRuntime core;
    bind_rom(core, RomView{rom.data(), rom.size()});
    reset(core);

    assert(actor_find_free_slot(core.ram,
                                ActorAllocationPool::CommonForward) == 3);
    actor_write8(actor_view(core.ram, 3), kActorTypeOffset, 0xFF);
    assert(actor_find_free_slot(core.ram,
                                ActorAllocationPool::CommonForward) == 4);
    assert(actor_find_free_slot(core.ram,
                                ActorAllocationPool::CommonReverse) == 20);
    assert(actor_find_free_slot(core.ram,
                                ActorAllocationPool::GameplayForward) == 1);

    const ActorView actor = actor_view(core.ram, 3);
    actor_write16(actor, kActorXOffset, 100);
    actor_write16(actor, kActorYOffset, 200);
    actor_write32(actor, kActorMovementLoopPcOffset, 0x11111111);
    actor_write8(actor, kActorMovementLoopTimerOffset, 7);
    actor_write32(actor, kActorMovementReturnPcOffset, 0x22222222);
    assert(actor_initialize_from_template(core, 3, kTemplate));
    assert(actor_read8(actor, kActorTypeOffset) == 0x42);
    assert(actor_read8(actor, kActorTimerOffset) == 7);
    assert(actor_read8(actor, kActorMovementFlagsOffset) == 0x40);
    assert(actor_read8(actor, kActorRuntimeField07Offset) == 0x12);
    assert(actor_read8(actor, kActorRuntimeField07Offset + 1) == 0x34);
    assert(actor_read8(actor, kActorFacingXOffset) == 0xFF);
    assert(actor_read32(actor, kActorMovementPcOffset) == 0x0012146C);
    assert(actor_read16(actor, 0x1E) == 0x6000);
    assert(actor_read32(actor, kActorAnimationPcOffset) == 0x0012437E);
    assert(actor_read8(actor, kActorResourceCountOffset) == 2);
    assert(actor_read8(actor, kActorFacingYOffset) == 1);
    assert(actor_read8(actor, kActorFlagsOffset) == 0x20);
    assert(actor_read16(actor, kActorXOffset) == 100);
    assert(actor_read16(actor, kActorYOffset) == 200);
    assert(actor_read32(actor, kActorMovementLoopPcOffset) == 0x11111111);
    assert(actor_read8(actor, kActorMovementLoopTimerOffset) == 7);
    assert(actor_read32(actor, kActorMovementReturnPcOffset) == 0x22222222);
    assert(actor_read32(actor, kActorFramePointerOffset) == 0);
    assert(actor_read32(actor, kActorLinkedRecordPointerOffset) == 0);

    assert(actor_allocate_sprite_resources(core, 3));
    assert(actor_read32(actor, kActorResourcePointerOffset)
        == kActorResourceBitmapBase);
    assert(actor_read32(actor, kActorSpriteVramBaseOffset) == 0x8600);
    assert(read8(core.ram, kActorResourceBitmapBase) == 0xFF);
    assert(read8(core.ram, kActorResourceBitmapBase + 1) == 0xFF);
    assert(read8(core.ram, kActorResourceBitmapBase + 2) == 0xFF);
    actor_clear_owned_resources(core, 3);
    assert(actor_read32(actor, kActorResourcePointerOffset) == 0);
    assert(actor_read32(actor, kActorSpriteVramBaseOffset) == 0);
    assert(read8(core.ram, kActorResourceBitmapBase) == 0);

    const auto spawned = actor_spawn_from_template(
        core, ActorAllocationPool::GameplayForward, kTemplate, 0x1234, 0x5678,
        ActorSpawnOverrides{0x0012271A, 0x0012146C, 0x84, true});
    assert(spawned && *spawned == 1);
    const ActorView child = actor_view(core.ram, *spawned);
    assert(actor_read8(child, kActorTypeOffset) == 0x84);
    assert(actor_read16(child, kActorXOffset) == 0x1234);
    assert(actor_read16(child, kActorYOffset) == 0x5678);
    assert(actor_read32(child, kActorAnimationPcOffset) == 0x0012271A);
    assert(actor_read32(child, kActorMovementPcOffset) == 0x0012146C);
    assert(actor_read32(child, kActorResourcePointerOffset)
        == kActorResourceBitmapBase);

    // Links are actor-record pointers, not host slot numbers. Shared cleanup
    // therefore resolves the pointer back to the direct RAM record.
    actor_write32(actor, kActorLinkedRecordPointerOffset,
                  actor_address(*spawned, 0));
    actor_write32(child, kActorLinkedRecordPointerOffset,
                  actor_address(3, 0));
    actor_clear_and_release(core, 3);
    assert(actor_read8(actor, kActorTypeOffset) == 0);
    assert(actor_read8(child, kActorTypeOffset) == 0);
    assert(actor_read32(child, kActorResourcePointerOffset) == 0);
    assert(read8(core.ram, kActorResourceBitmapBase) == 0);
    assert(actor_slot_for_address(actor_address(7, 0)) == 7);
    assert(!actor_slot_for_address(actor_address(7, 1)));

    return 0;
}
