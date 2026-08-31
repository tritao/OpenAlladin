#include "core/actor.hpp"

#include <array>

namespace openaladdin::core {
namespace {

struct SlotRange {
    int first;
    int last;
    int step;
};

SlotRange allocation_range(ActorAllocationPool pool) {
    switch (pool) {
    case ActorAllocationPool::CommonForward: return {3, 22, 1};
    case ActorAllocationPool::CommonReverse: return {20, 1, -1};
    case ActorAllocationPool::GameplayForward: return {1, 24, 1};
    case ActorAllocationPool::GameplayReverse: return {24, 1, -1};
    case ActorAllocationPool::AuxiliaryForward: return {25, 30, 1};
    }
    return {1, 0, 1};
}

bool template_is_available(const RomView rom, RamAddress address) {
    return rom_is_bound(rom) && rom.size >= 0x13
        && address <= rom.size - 0x13;
}

bool resource_pointer_index(
    std::uint32_t pointer,
    std::size_t& first
) {
    if (pointer < kActorResourceBitmapBase) return false;
    const std::uint32_t delta = pointer - kActorResourceBitmapBase;
    if (delta >= kActorResourceBitmapSize) return false;
    first = static_cast<std::size_t>(delta);
    return true;
}

void clear_template_transient_fields(GenesisRam& ram, std::size_t slot) {
    constexpr std::array<std::size_t, 14> fields = {
        0x13, 0x14, 0x18, 0x1A, 0x1C, 0x1D, 0x2A, 0x2E,
        0x32, 0x34, 0x36, 0x37, 0x3D, 0x3E,
    };
    for (const std::size_t offset : fields) {
        const std::size_t width = offset == 0x14 || offset == 0x2A
                || offset == 0x2E || offset == 0x3E
            ? 4 : offset == 0x1A || offset == 0x1C || offset == 0x1D
                ? (offset == 0x1A ? 2 : 1) : 1;
        if (width == 4) {
            actor_write32(actor_view(ram, slot), offset, 0);
        } else if (width == 2) {
            actor_write16(actor_view(ram, slot), offset, 0);
        } else {
            actor_write8(actor_view(ram, slot), offset, 0);
        }
    }
}

}  // namespace

std::optional<std::size_t> actor_find_free_slot(
    const GenesisRam& ram,
    ActorAllocationPool pool
) {
    const SlotRange range = allocation_range(pool);
    for (int slot = range.first;
         range.step > 0 ? slot <= range.last : slot >= range.last;
         slot += range.step) {
        if (actor_read8(actor_view(ram, static_cast<std::size_t>(slot)),
                        kActorTypeOffset) == 0) {
            return static_cast<std::size_t>(slot);
        }
    }
    return std::nullopt;
}

bool actor_initialize_from_template(
    CoreRuntime& core,
    std::size_t actor_slot,
    RamAddress template_address
) {
    if (!is_actor_slot(actor_slot)
        || !template_is_available(core.rom, template_address)) {
        return false;
    }

    const ActorView actor = actor_view(core.ram, actor_slot);
    actor_write8(actor, kActorTypeOffset,
                 rom_read8(core.rom, template_address));
    actor_write8(actor, kActorTimerOffset,
                 rom_read8(core.rom, template_address + 1));
    for (std::size_t index = 0; index < 4; ++index) {
        actor_write8(actor, 0x06 + index,
                     rom_read8(core.rom, template_address + 2 + index));
    }
    actor_write32(actor, kActorMovementPcOffset,
                  rom_read32(core.rom, template_address + 0x06));
    actor_write16(actor, 0x1E,
                  rom_read16(core.rom, template_address + 0x0A));
    actor_write32(actor, kActorAnimationPcOffset,
                  rom_read32(core.rom, template_address + 0x0C));
    actor_write8(actor, kActorResourceCountOffset,
                 rom_read8(core.rom, template_address + 0x10));
    actor_write8(actor, kActorFacingYOffset,
                 rom_read8(core.rom, template_address + 0x11));
    actor_write8(actor, kActorFlagsOffset,
                 rom_read8(core.rom, template_address + 0x12));
    clear_template_transient_fields(core.ram, actor_slot);
    return true;
}

bool actor_allocate_sprite_resources(CoreRuntime& core, std::size_t actor_slot) {
    if (!is_actor_slot(actor_slot)) return false;
    const ActorView actor = actor_view(core.ram, actor_slot);
    actor_clear_owned_resources(core, actor_slot);

    const std::size_t requested = static_cast<std::size_t>(
        actor_read8(actor, kActorResourceCountOffset)) + 1;
    if (requested > kActorResourceBitmapSize) return false;

    std::size_t first = 0;
    for (; first + requested <= kActorResourceBitmapSize; ++first) {
        bool free = true;
        for (std::size_t index = first; index < first + requested; ++index) {
            if (read8(core.ram, kActorResourceBitmapBase + index) != 0) {
                free = false;
                break;
            }
        }
        if (free) break;
    }
    if (first + requested > kActorResourceBitmapSize) return false;

    for (std::size_t index = first; index < first + requested; ++index) {
        write8(core.ram, kActorResourceBitmapBase + index, 0xFF);
    }
    actor_write32(actor, kActorResourcePointerOffset,
                  kActorResourceBitmapBase + static_cast<RamAddress>(first));
    actor_write32(actor, kActorSpriteVramBaseOffset,
                  rom_read32(core.rom,
                             kActorSpriteVramBaseTable
                                 + static_cast<RamAddress>(first * 4)));
    return true;
}

void actor_clear_owned_resources(CoreRuntime& core, std::size_t actor_slot) {
    if (!is_actor_slot(actor_slot)) return;
    const ActorView actor = actor_view(core.ram, actor_slot);
    std::size_t first = 0;
    const std::size_t count = static_cast<std::size_t>(
        actor_read8(actor, kActorResourceCountOffset)) + 1;
    if (resource_pointer_index(actor_read32(actor, kActorResourcePointerOffset), first)
        && count <= kActorResourceBitmapSize - first) {
        for (std::size_t index = first; index < first + count; ++index) {
            write8(core.ram, kActorResourceBitmapBase + index, 0);
        }
    }
    actor_write32(actor, kActorResourcePointerOffset, 0);
    actor_write32(actor, kActorSpriteVramBaseOffset, 0);
}

void actor_clear_type_and_release(CoreRuntime& core, std::size_t actor_slot) {
    if (!is_actor_slot(actor_slot)) return;
    const ActorView actor = actor_view(core.ram, actor_slot);
    actor_clear_owned_resources(core, actor_slot);
    actor_write8(actor, kActorTypeOffset, 0);
}

void actor_clear_and_release(CoreRuntime& core, std::size_t actor_slot) {
    if (!is_actor_slot(actor_slot)) return;
    const ActorView actor = actor_view(core.ram, actor_slot);
    const auto linked = actor_slot_for_address(actor_read32(
        actor, kActorLinkedRecordPointerOffset));
    actor_clear_type_and_release(core, actor_slot);
    if (linked && *linked != actor_slot) {
        actor_clear_type_and_release(core, *linked);
    }
}

std::optional<std::size_t> actor_spawn_from_template(
    CoreRuntime& core,
    ActorAllocationPool pool,
    RamAddress template_address,
    std::uint16_t x,
    std::uint16_t y,
    const ActorSpawnOverrides& overrides
) {
    const auto slot = actor_find_free_slot(core.ram, pool);
    if (!slot || !actor_initialize_from_template(core, *slot, template_address)) {
        return std::nullopt;
    }
    const ActorView actor = actor_view(core.ram, *slot);
    actor_write16(actor, kActorXOffset, x);
    actor_write16(actor, kActorYOffset, y);
    if (overrides.override_type) {
        actor_write8(actor, kActorTypeOffset, overrides.type);
    }
    if (overrides.animation_pc != 0) {
        actor_write32(actor, kActorAnimationPcOffset, overrides.animation_pc);
    }
    if (overrides.movement_pc != 0) {
        actor_write32(actor, kActorMovementPcOffset, overrides.movement_pc);
    }
    if (!actor_allocate_sprite_resources(core, *slot)) {
        actor_clear_and_release(core, *slot);
        return std::nullopt;
    }
    return slot;
}

}  // namespace openaladdin::core
