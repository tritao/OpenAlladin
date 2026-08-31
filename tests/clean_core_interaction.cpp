#include "core/interaction.hpp"

#include "core/trace.hpp"

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

void write_template(std::vector<std::uint8_t>& rom, std::size_t address,
                    std::uint8_t type, std::uint32_t movement,
                    std::uint32_t animation) {
    rom[address] = type;
    rom[address + 1] = 0x07;
    rom[address + 2] = 0x40;
    rom[address + 3] = 0x12;
    rom[address + 4] = 0x34;
    rom[address + 5] = 0xFF;
    write_u32(rom, address + 0x06, movement);
    write_u16(rom, address + 0x0A, 0x6000);
    write_u32(rom, address + 0x0C, animation);
    rom[address + 0x10] = 2;
    rom[address + 0x11] = 1;
    rom[address + 0x12] = 0x20;
}

void write_handler(std::vector<std::uint8_t>& rom, std::uint8_t selector,
                   std::uint32_t handler) {
    write_u32(
        rom,
        openaladdin::core::kInteractionHandlerTable
            + static_cast<std::size_t>(selector) * 4,
        handler);
}

}  // namespace

int main() {
    using namespace openaladdin::core;

    constexpr std::size_t kRomSize = 0x001B7C60;
    constexpr RamAddress kRow = 0x00FF2200;
    constexpr std::uint16_t kInteractionIndex = 0x0020;
    std::vector<std::uint8_t> rom(kRomSize, 0);
    write_template(rom, 0x001B7C10, 0x20, 0x00120400, 0x00123358);
    write_template(rom, 0x001B7C24, 0x1E, 0x00120410, 0x00123580);
    write_template(rom, 0x001B7C38, 0x1F, 0x00120420, 0x00123900);
    write_handler(rom, 0x1B, 0x001B6EB2);
    write_handler(rom, 0x1A, 0x001B6ED0);
    write_handler(rom, 0x19, 0x001B6EEE);

    CoreRuntime core;
    bind_rom(core, RomView{rom.data(), rom.size()});
    reset(core);
    write32(core.ram, kInteractionRowPointer, kRow);
    write16(core.ram, kInteractionHandlerX, 0x1000);
    write16(core.ram, kInteractionHandlerY, 0x0200);
    write16(core.ram, kInteractionSpawnXOffset, 0xFFF0);
    write16(core.ram, kInteractionSpawnYOffset, 0x00F0);
    write8(core.ram, kRow + kInteractionIndex, 0x1B);

    const InteractionDispatch unknown = interaction_dispatch(core, 0xFE);
    assert(unknown.in_range);
    assert(unknown.handler == 0);
    assert(unknown.no_op);

    CoreTrace trace;
    const InteractionSpawnResult type20 = interaction_spawn_dispatch(
        core, kInteractionIndex, 0x1B, &trace);
    assert(type20.handler_applied);
    assert(type20.actor_slot && *type20.actor_slot == 3);
    assert(trace.interaction_selector == 0x1B);
    assert(trace.interaction_handler == 0x001B6EB2);
    assert(trace.interaction_index == kInteractionIndex);
    assert(trace.interaction_spawn_slot == 3);
    const ActorView actor20 = actor_view(core.ram, 3);
    assert(actor_read8(actor20, kActorTypeOffset) == 0x20);
    assert(actor_read16(actor20, kActorXOffset) == 0x0FF0);
    assert(actor_read16(actor20, kActorYOffset) == 0x02F0);
    assert(actor_read16(actor20, 0x32) == kInteractionIndex);
    assert(actor_read8(actor20, 0x34) == 0x1B);
    assert(actor_read32(actor20, kActorAnimationPcOffset) == 0x0012337A);
    assert(read8(core.ram, kRow + kInteractionIndex) == 0);

    write8(core.ram, kRow + kInteractionIndex, 0x1A);
    const InteractionSpawnResult type21 = interaction_spawn_dispatch(
        core, kInteractionIndex, 0x1A);
    assert(type21.handler_applied);
    assert(type21.actor_slot && *type21.actor_slot == 4);
    const ActorView actor21 = actor_view(core.ram, 4);
    assert(actor_read8(actor21, kActorTypeOffset) == 0x21);
    assert(actor_read32(actor21, kActorAnimationPcOffset) == 0x001235AC);

    write8(core.ram, kRow + kInteractionIndex, 0x19);
    const InteractionSpawnResult type22 = interaction_spawn_dispatch(
        core, kInteractionIndex, 0x19);
    assert(type22.handler_applied);
    assert(type22.actor_slot && *type22.actor_slot == 5);
    const ActorView actor22 = actor_view(core.ram, 5);
    assert(actor_read8(actor22, kActorTypeOffset) == 0x22);
    assert(actor_read32(actor22, kActorMovementPcOffset) == 0);
    assert(actor_read32(actor22, kActorAnimationPcOffset) == 0x001238B2);

    // The preserve variant shares the placement/payload helper but leaves
    // the source interaction row available for the caller's later branch.
    write8(core.ram, kRow + 0x0021, 0x75);
    const auto preserved = interaction_allocate_preserve_row(
        core, ActorAllocationPool::CommonReverse, 0x001B7C10, 0x0021,
        0x75);
    assert(preserved && *preserved == 20);
    assert(read8(core.ram, kRow + 0x0021) == 0x75);
    const ActorView preserved_actor = actor_view(core.ram, *preserved);
    assert(actor_read16(preserved_actor, 0x32) == 0x0021);
    assert(actor_read8(preserved_actor, 0x34) == 0x75);

    const InteractionSpawnResult unimplemented = interaction_spawn_dispatch(
        core, 0x0022, 0xFE);
    assert(!unimplemented.handler_applied);
    assert(!unimplemented.actor_slot);

    return 0;
}
