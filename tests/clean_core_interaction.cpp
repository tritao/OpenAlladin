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

    constexpr std::size_t kRomSize = 0x001B83B8;
    constexpr RamAddress kRow = 0x00FF2200;
    constexpr std::uint16_t kInteractionIndex = 0x0020;
    std::vector<std::uint8_t> rom(kRomSize, 0);
    write_template(rom, 0x001B7C10, 0x20, 0x00120400, 0x00123358);
    write_template(rom, 0x001B7C24, 0x1E, 0x00120410, 0x00123580);
    write_template(rom, 0x001B7C38, 0x1F, 0x00120420, 0x00123900);
    write_template(rom, 0x001B7D14, 0x84, 0, 0x00122F4E);
    write_template(rom, 0x001B82B4, 0x84, 0, 0x00125CC6);
    write_template(rom, 0x001B7EF4, 0x32, 0, 0x00124616);
    write_template(rom, 0x001B7FD0, 0x62, 0, 0x00124CE0);
    write_template(rom, 0x001B7FE4, 0x62, 0, 0x00124CDC);
    write_template(rom, 0x001B7FBC, 0x64, 0x00120B36, 0x00124CD8);
    write_template(rom, 0x001B7FF8, 0x10, 0, 0x00124CE4);
    write_template(rom, 0x001B8084, 0x58, 0, 0x00125392);
    write_template(rom, 0x001B7A1C, 0x55, 0, 0x00122D54);
    write_template(rom, 0x001B7E68, 0x74, 0, 0x001244E6);
    write_template(rom, 0x001B8070, 0x07, 0x001216C6, 0x001252F0);
    write_template(rom, 0x001B7E2C, 0x8C, 0, 0x00124408);
    write_template(rom, 0x001B7DB4, 0x76, 0, 0x00124194);
    write_template(rom, 0x001B7DDC, 0x84, 0, 0x00124226);
    write_template(rom, 0x001B7DF0, 0x84, 0, 0x00124226);
    write_template(rom, 0x001B7DA0, 0x89, 0, 0x001241F8);
    write_template(rom, 0x001B7D8C, 0x6A, 0, 0x00124046);
    write_template(rom, 0x001B7D78, 0x69, 0, 0x00124046);
    write_template(rom, 0x001B7AE4, 0x23, 0, 0x00123198);
    write_template(rom, 0x001B7B34, 0x06, 0, 0x00123200);
    write_template(rom, 0x001B7B0C, 0x2B, 0, 0x00123FE4);
    write_template(rom, 0x001B8354, 0x84, 0, 0x00125DEA);
    write_template(rom, 0x001B7C9C, 0x84, 0, 0x00123E76);
    write_template(rom, 0x001B78B4, 0x5F, 0, 0x00124D00);
    write_template(rom, 0x001B7C4C, 0x87, 0, 0x00123B38);
    write_handler(rom, 0x1B, 0x001B6EB2);
    write_handler(rom, 0x1A, 0x001B6ED0);
    write_handler(rom, 0x19, 0x001B6EEE);
    write_handler(rom, 0x20, 0x001B65C0);
    write_handler(rom, 0xA4, 0x001B65E0);
    write_handler(rom, 0x35, 0x001B6636);
    write_handler(rom, 0x36, 0x001B6654);
    write_handler(rom, 0x70, 0x001B6836);
    write_handler(rom, 0x82, 0x001B68D6);
    write_handler(rom, 0x80, 0x001B6F0C);

    CoreRuntime core;
    bind_rom(core, RomView{rom.data(), rom.size()});
    reset(core);
    write32(core.ram, kInteractionRowPointer, kRow);
    write16(core.ram, kInteractionHandlerX, 0x1000);
    write16(core.ram, kInteractionHandlerY, 0x0200);
    write16(core.ram, kInteractionSpawnXOffset, 0xFFF0);
    write16(core.ram, kInteractionSpawnYOffset, 0x00F0);
    write8(core.ram, kInteractionRuntimeTable + kInteractionIndex, 0x1B);

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
    assert(read8(core.ram, kInteractionRuntimeTable + kInteractionIndex) == 0);

    for (std::size_t slot = 1; slot < kActorSlotCount; ++slot) {
        actor_write8(actor_view(core.ram, slot), kActorTypeOffset, 0);
    }
    write8(core.ram, kInteractionRuntimeTable + kInteractionIndex, 0x20);
    const InteractionSpawnResult offset84 = interaction_spawn_dispatch(
        core, kInteractionIndex, 0x20);
    assert(offset84.handler_applied);
    assert(offset84.actor_slot && *offset84.actor_slot == 24);
    const ActorView actor84 = actor_view(core.ram, 24);
    assert(actor_read8(actor84, kActorTypeOffset) == 0x84);
    assert(actor_read16(actor84, kActorYOffset) == 0x02FA);

    for (std::size_t slot = 1; slot < kActorSlotCount; ++slot) {
        actor_write8(actor_view(core.ram, slot), kActorTypeOffset, 0);
    }
    write8(core.ram, kInteractionRuntimeTable + kInteractionIndex, 0x36);
    const InteractionSpawnResult presentation = interaction_spawn_dispatch(
        core, kInteractionIndex, 0x36);
    assert(presentation.handler_applied);
    assert(presentation.actor_slot && *presentation.actor_slot == 20);
    const ActorView presentation_actor = actor_view(core.ram, 20);
    assert(actor_read8(presentation_actor, kActorTypeOffset) == 0x1B);
    assert(actor_read8(presentation_actor, kActorFacingXOffset) == 0xFF);
    assert(actor_read16(presentation_actor, kActorXOffset) == 0x0FF0);
    assert(actor_read16(presentation_actor, kActorYOffset) == 0x0300);

    for (std::size_t slot = 1; slot < kActorSlotCount; ++slot) {
        actor_write8(actor_view(core.ram, slot), kActorTypeOffset, 0);
    }
    write8(core.ram, kInteractionRuntimeTable + kInteractionIndex, 0x70);
    write8(core.ram, kActorCollisionEventFlag, 1);
    const InteractionSpawnResult blocked = interaction_spawn_dispatch(
        core, kInteractionIndex, 0x70);
    assert(!blocked.handler_applied);
    write8(core.ram, kActorCollisionEventFlag, 0);
    const InteractionSpawnResult type6c = interaction_spawn_dispatch(
        core, kInteractionIndex, 0x70);
    assert(type6c.handler_applied);
    assert(type6c.actor_slot && *type6c.actor_slot == 24);
    const ActorView actor6c = actor_view(core.ram, 24);
    assert(actor_read8(actor6c, kActorTypeOffset) == 0x6C);
    assert(actor_read16(actor6c, kActorXOffset) == 0x0FFF);

    for (std::size_t slot = 1; slot < kActorSlotCount; ++slot) {
        actor_write8(actor_view(core.ram, slot), kActorTypeOffset, 0);
    }
    write8(core.ram, kInteractionRuntimeTable + kInteractionIndex, 0x80);
    write8(core.ram, kInteractionResponseFlag, 0xFF);
    const InteractionSpawnResult response = interaction_spawn_dispatch(
        core, kInteractionIndex, 0x80);
    assert(response.handler_applied);
    assert(read8(core.ram, kInteractionResponseFlag) == 0);
    assert(response.actor_slot && *response.actor_slot == 20);

    // Keep the original forward-allocation assertions deterministic after
    // the reverse-slot response above.
    actor_write8(actor_view(core.ram, 3), kActorTypeOffset, 0xFF);
    write8(core.ram, kInteractionRuntimeTable + kInteractionIndex, 0x1A);
    const InteractionSpawnResult type21 = interaction_spawn_dispatch(
        core, kInteractionIndex, 0x1A);
    assert(type21.handler_applied);
    assert(type21.actor_slot && *type21.actor_slot == 4);
    const ActorView actor21 = actor_view(core.ram, 4);
    assert(actor_read8(actor21, kActorTypeOffset) == 0x21);
    assert(actor_read32(actor21, kActorAnimationPcOffset) == 0x001235AC);

    write8(core.ram, kInteractionRuntimeTable + kInteractionIndex, 0x19);
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
    actor_write8(actor_view(core.ram, 20), kActorTypeOffset, 0);
    write8(core.ram, kInteractionRuntimeTable + 0x0021, 0x75);
    const auto preserved = interaction_allocate_preserve_row(
        core, ActorAllocationPool::CommonReverse, 0x001B7C10, 0x0021,
        0x75);
    assert(preserved && *preserved == 20);
    assert(read8(core.ram, kInteractionRuntimeTable + 0x0021) == 0x75);
    const ActorView preserved_actor = actor_view(core.ram, *preserved);
    assert(actor_read16(preserved_actor, 0x32) == 0x0021);
    assert(actor_read8(preserved_actor, 0x34) == 0x75);

    const InteractionSpawnResult unimplemented = interaction_spawn_dispatch(
        core, 0x0022, 0xFE);
    assert(!unimplemented.handler_applied);
    assert(!unimplemented.actor_slot);

    // The row bodies are refill-triggered ROM windows, not an unconditional
    // per-frame scan. Exercise both wrapper/core coordinate profiles against
    // the same RAM-backed source row and selector table.
    reset(core);
    write32(core.ram, kInteractionRowPointer, kRow);
    write16(core.ram, kInteractionRowStride, 0x0020);
    write16(core.ram, kCameraReferenceX, 0x1234);
    write16(core.ram, kCameraReferenceY, 0x0456);
    write16(core.ram, kRow, 0x0020);
    write8(core.ram, kInteractionRuntimeTable + 0x0010, 0x1B);
    const InteractionRowPassResult rows_a = interaction_process_rows_a(core);
    assert(rows_a.rows_visited == 16);
    assert(rows_a.selector_count == 1);
    assert(rows_a.spawn_count == 1);
    assert(actor_read8(actor_view(core.ram, 3), kActorTypeOffset) == 0x20);
    assert(actor_read16(actor_view(core.ram, 3), kActorXOffset) == 0x1220);
    assert(actor_read16(actor_view(core.ram, 3), kActorYOffset) == 0x0540);
    assert(read8(core.ram, kInteractionRuntimeTable + 0x0010) == 0);
    assert(read16(core.ram, kInteractionHandlerX) == 0x1230);
    assert(read16(core.ram, kInteractionHandlerY) == 0x0450);

    reset(core);
    write32(core.ram, kInteractionRowPointer, kRow);
    write16(core.ram, kInteractionRowStride, 0x0020);
    write16(core.ram, kCameraReferenceX, 0x1234);
    write16(core.ram, kCameraReferenceY, 0x0456);
    write16(core.ram, kRow, 0x0020);
    write8(core.ram, kInteractionRuntimeTable + 0x0010, 0x1B);
    const InteractionRowPassResult rows_a_core =
        interaction_process_rows_a_core(core);
    assert(rows_a_core.rows_visited == 16);
    assert(rows_a_core.selector_count == 1);
    assert(rows_a_core.spawn_count == 1);
    assert(actor_read16(actor_view(core.ram, 3), kActorXOffset) == 0x1380);
    assert(actor_read16(actor_view(core.ram, 3), kActorYOffset) == 0x0540);
    assert(read16(core.ram, kInteractionHandlerX) == 0x1230);
    assert(read16(core.ram, kInteractionHandlerY) == 0x0450);

    reset(core);
    write32(core.ram, kInteractionRowPointer, kRow);
    write16(core.ram, kCameraReferenceX, 0x1234);
    write16(core.ram, kCameraReferenceY, 0x0456);
    write16(core.ram, kRow, 0x0040);
    write8(core.ram, kInteractionRuntimeTable + 0x0020, 0x1B);
    const InteractionRowPassResult rows_b = interaction_process_rows_b(core);
    assert(rows_b.rows_visited == 23);
    assert(rows_b.selector_count == 1);
    assert(rows_b.spawn_count == 1);
    assert(actor_read16(actor_view(core.ram, 3), kActorXOffset) == 0x1220);
    assert(actor_read16(actor_view(core.ram, 3), kActorYOffset) == 0x0540);
    assert(read16(core.ram, kInteractionHandlerX) == 0x1230);
    assert(read16(core.ram, kInteractionHandlerY) == 0x0450);

    reset(core);
    write32(core.ram, kInteractionRowPointer, kRow);
    write16(core.ram, kCameraReferenceX, 0x1234);
    write16(core.ram, kCameraReferenceY, 0x0456);
    write16(core.ram, kRow, 0x0040);
    write8(core.ram, kInteractionRuntimeTable + 0x0020, 0x1B);
    const InteractionRowPassResult rows_b_core =
        interaction_process_rows_b_core(core);
    assert(rows_b_core.rows_visited == 23);
    assert(rows_b_core.selector_count == 1);
    assert(rows_b_core.spawn_count == 1);
    assert(actor_read16(actor_view(core.ram, 3), kActorXOffset) == 0x1220);
    assert(actor_read16(actor_view(core.ram, 3), kActorYOffset) == 0x0630);
    assert(read16(core.ram, kInteractionHandlerX) == 0x1230);
    assert(read16(core.ram, kInteractionHandlerY) == 0x0450);

    return 0;
}
