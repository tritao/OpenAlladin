#include "core/collision.hpp"

#include "core/actor.hpp"

namespace openaladdin::core {
namespace {

constexpr RamAddress kActorCollisionToggleFacing = 0x001AC60E;
constexpr RamAddress kPlayerCollisionNoopType0D = 0x001AEB7A;
constexpr RamAddress kPlayerCollisionNoopType2B = 0x001AEBFE;
constexpr RamAddress kPlayerCollisionNoopType04 = 0x001AEDA6;
constexpr RamAddress kPlayerCollisionNoopType24To28 = 0x001AEEDE;
constexpr RamAddress kActorCollisionNoopType01 = 0x001ABF9A;

bool is_player_noop(RamAddress handler) {
    switch (handler) {
    case kPlayerCollisionNoopType0D:
    case kPlayerCollisionNoopType2B:
    case kPlayerCollisionNoopType04:
    case kPlayerCollisionNoopType24To28:
        return true;
    default:
        return false;
    }
}

bool is_actor_noop(RamAddress handler) {
    return handler == kActorCollisionNoopType01;
}

CollisionDispatch read_dispatch(
    const CoreRuntime& core,
    std::uint8_t actor_type,
    RamAddress table,
    std::size_t count,
    bool actor_table
) {
    CollisionDispatch dispatch;
    dispatch.actor_type = actor_type;
    if (actor_type >= count) return dispatch;

    dispatch.in_range = true;
    dispatch.handler = rom_read32(
        core.rom, table + static_cast<RamAddress>(actor_type) * 4U);
    dispatch.no_op = dispatch.handler == 0
        || (actor_table ? is_actor_noop(dispatch.handler)
                        : is_player_noop(dispatch.handler));
    return dispatch;
}

}  // namespace

CollisionDispatch player_collision_dispatch(
    const CoreRuntime& core,
    std::uint8_t actor_type
) {
    return read_dispatch(
        core, actor_type, kPlayerCollisionHandlerTable,
        kPlayerCollisionHandlerCount, false);
}

CollisionDispatch actor_collision_dispatch(
    const CoreRuntime& core,
    std::uint8_t receiving_type
) {
    return read_dispatch(
        core, receiving_type, kActorCollisionHandlerTable,
        kActorCollisionHandlerCount, true);
}

bool actor_collision_toggle_facing(CoreRuntime& core, std::size_t actor_slot) {
    if (!is_actor_slot(actor_slot)) return false;
    const ActorView actor = actor_view(core.ram, actor_slot);
    const std::uint8_t flags = actor_read8(actor, kActorMovementFlagsOffset);
    if ((flags & 0x40U) != 0) return false;

    actor_write8(actor, kActorMovementFlagsOffset,
                 static_cast<std::uint8_t>(flags | 0x40U));
    actor_write8(actor, kActorFacingXOffset,
                 static_cast<std::uint8_t>(
                     actor_read8(actor, kActorFacingXOffset) ^ 0xFFU));
    return true;
}

bool actor_collision_apply(
    CoreRuntime& core,
    std::size_t source_slot,
    std::size_t receiving_slot
) {
    if (!is_actor_slot(source_slot) || !is_actor_slot(receiving_slot)) {
        return false;
    }

    const std::uint8_t receiving_type = actor_read8(
        actor_view(core.ram, receiving_slot), kActorTypeOffset);
    const CollisionDispatch dispatch = actor_collision_dispatch(
        core, receiving_type);
    if (!dispatch.in_range || dispatch.no_op) return false;

    if (dispatch.handler == kActorCollisionToggleFacing) {
        return actor_collision_toggle_facing(core, receiving_slot);
    }

    // Other handlers have table identity but their shared response/resource
    // mutations are added only as each recovered contract is ported.
    return false;
}

}  // namespace openaladdin::core
