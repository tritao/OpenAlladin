#include "core/collision.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

void write_u32(std::vector<std::uint8_t>& rom, std::size_t offset,
               std::uint32_t value) {
    rom[offset] = static_cast<std::uint8_t>(value >> 24);
    rom[offset + 1] = static_cast<std::uint8_t>(value >> 16);
    rom[offset + 2] = static_cast<std::uint8_t>(value >> 8);
    rom[offset + 3] = static_cast<std::uint8_t>(value);
}

void write_frame(std::vector<std::uint8_t>& rom, std::size_t offset,
                 std::uint8_t left, std::uint8_t top,
                 std::uint8_t right, std::uint8_t bottom) {
    rom[offset + 2] = left;
    rom[offset + 3] = top;
    rom[offset + 4] = right;
    rom[offset + 5] = bottom;
}

}  // namespace

int main() {
    using namespace openaladdin::core;

    constexpr std::size_t kRomSize = 0x200000;
    constexpr std::size_t kPlayerFrame = 0x200;
    constexpr std::size_t kActorFrame = 0x220;
    std::vector<std::uint8_t> rom(kRomSize, 0);
    write_u32(rom, kPlayerCollisionHandlerTable + 0x04 * 4,
              0x001AEDA6);
    write_u32(rom, kPlayerCollisionHandlerTable + 0x0D * 4,
              0x001AEB7A);
    write_u32(rom, kPlayerCollisionHandlerTable + 0x11 * 4,
              0x001AF110);
    write_u32(rom, kActorCollisionHandlerTable + 0x01 * 4,
              0x001ABF9A);
    write_u32(rom, kActorCollisionHandlerTable + 0x0D * 4,
              0x001AC60E);
    write_frame(rom, kPlayerFrame, 0xF0, 0xF0, 0x10, 0x10);
    write_frame(rom, kActorFrame, 0xF0, 0xF0, 0x10, 0x10);

    CoreRuntime core;
    bind_rom(core, RomView{rom.data(), rom.size()});
    reset(core);

    const CollisionDispatch player_noop = player_collision_dispatch(core, 0x04);
    assert(player_noop.in_range);
    assert(player_noop.handler == 0x001AEDA6);
    assert(player_noop.no_op);

    const CollisionDispatch player_response =
        player_collision_dispatch(core, 0x11);
    assert(player_response.in_range);
    assert(player_response.handler == 0x001AF110);
    assert(!player_response.no_op);

    assert(!player_collision_dispatch(core, 0x7F).in_range);
    assert(!actor_collision_dispatch(core, 0x32).in_range);

    const CollisionDispatch actor_noop = actor_collision_dispatch(core, 0x01);
    assert(actor_noop.in_range);
    assert(actor_noop.handler == 0x001ABF9A);
    assert(actor_noop.no_op);

    const CollisionDispatch actor_toggle = actor_collision_dispatch(core, 0x0D);
    assert(actor_toggle.in_range);
    assert(actor_toggle.handler == 0x001AC60E);
    assert(!actor_toggle.no_op);

    const ActorView receiver = actor_view(core.ram, 4);
    actor_write8(receiver, kActorTypeOffset, 0x0D);
    const ActorView source_actor = actor_view(core.ram, 3);
    actor_write8(source_actor, kActorFacingXOffset, 0x00);
    actor_write8(source_actor, kActorMovementFlagsOffset, 0x00);
    assert(actor_collision_apply(core, 3, 4));
    assert(actor_read8(source_actor, kActorFacingXOffset) == 0xFF);
    assert(actor_read8(source_actor, kActorMovementFlagsOffset) == 0x40);
    assert(!actor_collision_apply(core, 3, 4));
    assert(actor_read8(source_actor, kActorFacingXOffset) == 0xFF);

    // The pass uses the ROM frame records and recovered pools directly. A
    // player/common overlap publishes the candidate type, while an
    // auxiliary/common overlap reaches the Type-0D actor response.
    const ActorView player = actor_view(core.ram, 0);
    actor_write32(player, kActorFramePointerOffset, kPlayerFrame);
    actor_write16(player, kActorXOffset, 100);
    actor_write16(player, kActorYOffset, 100);
    write16(core.ram, kWorldCameraX, 0);
    write16(core.ram, kWorldCameraY, 0);
    write16(core.ram, kPlayerX, 100);
    write16(core.ram, kPlayerY, 100);
    actor_write8(receiver, kActorTypeOffset, 0x04);
    actor_write32(receiver, kActorFramePointerOffset, kActorFrame);
    actor_write16(receiver, kActorXOffset, 100);
    actor_write16(receiver, kActorYOffset, 100);
    const CollisionPassResult player_result = player_collision_pass(core);
    assert(player_result.contact_count == 1);
    assert(player_result.dispatch.handler == 0x001AEDA6);
    assert(read8(core.ram, kPlayerCollisionCurrentActorType) == 0x04);

    const ActorView source = actor_view(core.ram, 25);
    actor_write8(source, kActorTypeOffset, 0x80);
    actor_write32(source, kActorFramePointerOffset, kActorFrame);
    actor_write16(source, kActorXOffset, 100);
    actor_write16(source, kActorYOffset, 100);
    actor_write8(receiver, kActorTypeOffset, 0x0D);
    actor_write8(receiver, kActorMovementFlagsOffset, 0);
    actor_write8(receiver, kActorFacingXOffset, 0);
    const CollisionPassResult actor_result = actor_collision_pass(core);
    assert(actor_result.contact_count == 1);
    assert(actor_result.dispatch.handler == 0x001AC60E);
    assert(actor_result.handler_applied);
    assert(actor_read8(source, kActorFacingXOffset) == 0xFF);
    assert(actor_read8(source, kActorMovementFlagsOffset) == 0x40);

    return 0;
}
