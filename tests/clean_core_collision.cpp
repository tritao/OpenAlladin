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

}  // namespace

int main() {
    using namespace openaladdin::core;

    constexpr std::size_t kRomSize = 0x200000;
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
    actor_write8(receiver, kActorFacingXOffset, 0x00);
    actor_write8(receiver, kActorMovementFlagsOffset, 0x00);
    assert(actor_collision_apply(core, 3, 4));
    assert(actor_read8(receiver, kActorFacingXOffset) == 0xFF);
    assert(actor_read8(receiver, kActorMovementFlagsOffset) == 0x40);
    assert(!actor_collision_apply(core, 3, 4));
    assert(actor_read8(receiver, kActorFacingXOffset) == 0xFF);

    return 0;
}
