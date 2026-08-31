#include "core/collision.hpp"

#include "core/actor.hpp"

#include <algorithm>

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

int normalized_left(const CollisionBox& box) {
    return std::min(box.left, box.right);
}

int normalized_right(const CollisionBox& box) {
    return std::max(box.left, box.right);
}

int normalized_top(const CollisionBox& box) {
    return std::min(box.top, box.bottom);
}

int normalized_bottom(const CollisionBox& box) {
    return std::max(box.top, box.bottom);
}

int signed_frame_byte(std::uint8_t value) {
    return static_cast<int>(static_cast<std::int8_t>(value));
}

std::uint8_t negated_frame_byte(std::uint8_t value) {
    return static_cast<std::uint8_t>(-signed_frame_byte(value));
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

CollisionBox collision_frame_box(
    const CoreRuntime& core,
    RamAddress frame_pointer,
    int origin_x,
    int origin_y,
    std::uint8_t facing_x_flip
) {
    CollisionBox box;
    if (!rom_is_bound(core.rom)
        || frame_pointer > core.rom.size
        || core.rom.size - frame_pointer < 6) {
        return box;
    }

    const std::uint8_t left_offset = rom_read8(core.rom, frame_pointer + 2);
    const std::uint8_t top_offset = rom_read8(core.rom, frame_pointer + 3);
    const std::uint8_t right_offset = rom_read8(core.rom, frame_pointer + 4);
    const std::uint8_t bottom_offset = rom_read8(core.rom, frame_pointer + 5);
    if (facing_x_flip == 0) {
        box.left = origin_x + left_offset;
        box.right = origin_x + right_offset;
    } else {
        // The collision routine negates the signed frame byte and stores the
        // result as a byte before adding the actor origin.
        box.left = origin_x + negated_frame_byte(right_offset);
        box.right = origin_x + negated_frame_byte(left_offset);
    }
    box.top = origin_y + top_offset;
    box.bottom = origin_y + bottom_offset;
    box.valid = true;
    return box;
}

bool collision_overlaps(const CollisionBox& first, const CollisionBox& second) {
    if (!first.valid || !second.valid) return false;
    return normalized_left(first) <= normalized_right(second)
        && normalized_top(first) <= normalized_bottom(second)
        && normalized_left(second) < normalized_right(first)
        && normalized_top(second) < normalized_bottom(first);
}

CollisionPassResult player_collision_pass(CoreRuntime& core) {
    CollisionPassResult result;
    write8(core.ram, kPlayerCollisionResponseSuppress, 0);
    write8(core.ram, kPlayerCollisionCurrentActorType, 0);

    const GenesisRam& ram = core.ram;
    const ConstActorView player = actor_view(ram, 0);
    const RamAddress player_frame = actor_read32(
        player, kActorFramePointerOffset);
    const CollisionBox player_box = collision_frame_box(
        core, player_frame, read16(core.ram, kPlayerWorldX),
        read16(core.ram, kPlayerWorldY),
        actor_read8(player, kActorFacingXOffset));
    if (!player_box.valid) return result;

    for (std::size_t slot = 1; slot <= 24; ++slot) {
        const ConstActorView actor = actor_view(ram, slot);
        const std::uint8_t type = actor_read8(actor, kActorTypeOffset);
        if (type == 0 || type >= 0x7F) continue;
        const CollisionBox actor_box = collision_frame_box(
            core, actor_read32(actor, kActorFramePointerOffset),
            actor_read16(actor, kActorXOffset),
            actor_read16(actor, kActorYOffset),
            actor_read8(actor, kActorFacingXOffset));
        if (!collision_overlaps(player_box, actor_box)) continue;

        write8(core.ram, kPlayerCollisionCurrentActorType, type);
        result.contact_count++;
        result.source_slot = 0;
        result.receiving_slot = slot;
        result.dispatch = player_collision_dispatch(core, type);
    }
    return result;
}

CollisionPassResult actor_collision_pass(CoreRuntime& core) {
    CollisionPassResult result;
    const GenesisRam& ram = core.ram;
    for (std::size_t source_slot = 25; source_slot <= 31; ++source_slot) {
        const ConstActorView source = actor_view(ram, source_slot);
        const std::uint8_t source_type = actor_read8(
            source, kActorTypeOffset);
        if (source_type == 0 || source_type >= 0x83) continue;
        const CollisionBox source_box = collision_frame_box(
            core, actor_read32(source, kActorFramePointerOffset),
            actor_read16(source, kActorXOffset),
            actor_read16(source, kActorYOffset),
            actor_read8(source, kActorFacingXOffset));
        if (!source_box.valid) continue;

        for (std::size_t receiver_slot = 1; receiver_slot <= 24;
             ++receiver_slot) {
            const ConstActorView receiver = actor_view(ram, receiver_slot);
            const std::uint8_t receiver_type = actor_read8(
                receiver, kActorTypeOffset);
            if (receiver_type == 0 || receiver_type >= 0x32) continue;
            const CollisionBox receiver_box = collision_frame_box(
                core, actor_read32(receiver, kActorFramePointerOffset),
                actor_read16(receiver, kActorXOffset),
                actor_read16(receiver, kActorYOffset),
                actor_read8(receiver, kActorFacingXOffset));
            if (!collision_overlaps(source_box, receiver_box)) continue;

            result.contact_count++;
            result.source_slot = source_slot;
            result.receiving_slot = receiver_slot;
            result.dispatch = actor_collision_dispatch(core, receiver_type);
            result.handler_applied = actor_collision_apply(
                core, source_slot, receiver_slot);
        }
    }
    return result;
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
