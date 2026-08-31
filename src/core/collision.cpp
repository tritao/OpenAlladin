#include "core/collision.hpp"

#include "core/actor.hpp"

#include <algorithm>

namespace openaladdin::core {
namespace {

constexpr RamAddress kActorCollisionToggleFacing = 0x001AC60E;
constexpr RamAddress kActorCollisionClearSource = 0x001ABF8E;
constexpr RamAddress kActorCollisionType14 = 0x001AC614;
constexpr RamAddress kActorCollisionType2B = 0x001AC63C;
constexpr RamAddress kActorCollisionType2F = 0x001AC676;
constexpr RamAddress kActorCollisionType30 = 0x001AC682;
constexpr RamAddress kActorCollisionType2D2E31 = 0x001AC6A2;
constexpr RamAddress kActorTemplateType84 = 0x001B7E40;
constexpr RamAddress kActorTemplateType2DInteraction = 0x001B792C;
constexpr RamAddress kActorTemplateType8D = 0x001B7CD8;
constexpr RamAddress kActorTemplateCollisionResponse = 0x001B7940;
constexpr RamAddress kActorAnimationType2B = 0x00123FF8;
constexpr RamAddress kActorAnimationType30 = 0x00123024;
constexpr RamAddress kActorAnimationType2D2E31 = 0x00122B6E;
constexpr RamAddress kPlayerHandlerType0C = 0x001AE9A8;
constexpr RamAddress kPlayerHandlerType1A = 0x001AE9E0;
constexpr RamAddress kPlayerHandlerType1B = 0x001AEA00;
constexpr RamAddress kPlayerHandlerType1C = 0x001AEA24;
constexpr RamAddress kPlayerTemplateCollisionType84 = 0x001B7CC4;
constexpr RamAddress kPlayerTemplateDeathType84 = 0x001B7940;
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

void clear_map_words_for_behaviors(
    GenesisRam& ram,
    std::uint8_t first_behavior,
    std::uint8_t second_behavior
) {
    // The two ROM helpers walk 0x3840 map words from FF0000 and use the
    // shifted map value as an index into the loaded behavior table at
    // FFAE86. Matching words are cleared in place.
    for (std::size_t offset = 0; offset < 0x7080; offset += 2) {
        const RamAddress map_address = kWorkRamBase
            + static_cast<RamAddress>(offset);
        const std::size_t behavior_index = read16(ram, map_address) >> 1;
        const std::uint8_t behavior = read8(
            ram, 0x00FFAE86 + static_cast<RamAddress>(behavior_index));
        if (behavior == first_behavior || behavior == second_behavior) {
            write16(ram, map_address, 0);
        }
    }
}

void player_collision_settle(
    CoreRuntime& core,
    std::size_t actor_slot
) {
    const ActorView actor = actor_view(core.ram, actor_slot);
    if ((actor_read8(actor, kActorMovementFlagsOffset) & 0x10U) == 0) {
        return;
    }
    if (read8(core.ram, kPlayerCollisionResponseSuppress) != 0) {
        return;
    }
    if (read8(core.ram, kPlayerTerrainResponseActive) != 0) {
        if (read8(core.ram, kPlayerTerrainVerticalStop) == 0) return;
        write8(core.ram, kPlayerTerrainResponseActive, 0);
    }
    if (read8(core.ram, kPlayerInteractionAnimationGate) != 0) return;

    if (read16(core.ram, kPlayerVelocityY) != 0) {
        write8(core.ram, kPlayerTerrainBrakeState, 0);
        const std::uint8_t type = actor_read8(actor, kActorTypeOffset);
        if (type >= 0x50 && type < 0x52) {
            write32(core.ram, kPlayerAnimationPc, 0x00121964);
        } else if (read8(core.ram, kCameraSpecialMode) != 0) {
            write32(core.ram, kPlayerAnimationPc, 0x00121F74);
        } else {
            write32(core.ram, kPlayerAnimationPc, 0x001220AA);
            if (read_i16(core.ram, kPlayerTerrainHorizontalResponse) == 0
                && read8(core.ram, kPlayerTerrainBounceAnimationState)
                    >= 0x28) {
                write32(core.ram, kPlayerAnimationPc, 0x00121BB6);
            }
        }
    }

    write16(core.ram, kPlayerVelocityY, 0);
    write8(core.ram, kPlayerAnimationTimer, 0);
    write8(core.ram, kPlayerTerrainResponseTimer, 0);
    write8(core.ram, kPlayerTerrainBounceAnimationState, 0);
    write8(core.ram, kPlayerTerrainLandingState, 0xFF);
    write8(core.ram, kPlayerInteractionMarker,
           actor_read8(actor, kActorTypeOffset));
    write8(core.ram, kPlayerInteractionMode, 0xFF);
    player_publish_world_coordinates(core);
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
    write8(core.ram, kPlayerCollisionCurrentActorType, 0);
    if (read8(core.ram, kPlayerTerrainResponsePassTimer) != 0) {
        write8(core.ram, kPlayerTerrainResponsePassTimer,
               static_cast<std::uint8_t>(
                   read8(core.ram, kPlayerTerrainResponsePassTimer) - 1));
    }
    if (read8(core.ram, kPlayerInteractionLock) != 0) {
        write8(core.ram, kPlayerInteractionLock,
               static_cast<std::uint8_t>(
                   read8(core.ram, kPlayerInteractionLock) - 1));
    }
    write8(core.ram, kPlayerInteractionResponse,
           read8(core.ram, kPlayerInteractionMarker));
    write8(core.ram, kPlayerInteractionMarker, 0);
    write8(core.ram, kPlayerInteractionMode, 0);

    const GenesisRam& ram = core.ram;
    const ConstActorView player = actor_view(ram, 0);
    const RamAddress player_frame = actor_read32(
        player, kActorFramePointerOffset);
    const CollisionBox player_box = collision_frame_box(
        core, player_frame, read16(core.ram, kPlayerX),
        read16(core.ram, kPlayerY),
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

        write8(core.ram, kPlayerCollisionResponseSuppress, 0);
        write8(core.ram, kPlayerCollisionCurrentActorType, type);
        result.contact_count++;
        result.source_slot = 0;
        result.receiving_slot = slot;
        result.dispatch = player_collision_dispatch(core, type);
        result.handler_applied = player_collision_apply(
            core, slot, result.dispatch);
        player_collision_settle(core, slot);
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
            0);
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
                0);
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

bool player_collision_apply(
    CoreRuntime& core,
    std::size_t actor_slot,
    const CollisionDispatch& dispatch
) {
    if (!is_actor_slot(actor_slot) || !dispatch.in_range || dispatch.no_op) {
        return false;
    }

    if (dispatch.handler == kPlayerHandlerType0C) {
        if (read8(core.ram, kPlayerActionResponseField) == 0) return false;
        // This body clears only the colliding record's type before calling
        // Actor_ClearOwnedResources. The linked-record cleanup belongs to
        // the neighboring Type-1A/1B/1C handlers.
        actor_clear_type_and_release(core, actor_slot);
        return actor_initialize_from_template(
            core, actor_slot, kPlayerTemplateCollisionType84);
    }

    if (dispatch.handler == kPlayerHandlerType1A) {
        if (read8(core.ram, kPlayerActionResponseField) == 0) return false;
        write8(core.ram, kPlayerInteractionType1ALatch, 0xFF);
        actor_clear_and_release(core, actor_slot);
        return actor_initialize_from_template(
            core, actor_slot, kPlayerTemplateDeathType84);
    }

    if (dispatch.handler == kPlayerHandlerType1B
        || dispatch.handler == kPlayerHandlerType1C) {
        if (read8(core.ram, kPlayerActionResponseField) == 0) return false;
        if (dispatch.handler == kPlayerHandlerType1B) {
            clear_map_words_for_behaviors(core.ram, 0x4F, 0xFE);
            write8(core.ram, kPlayerInteractionType1BLatch, 0xFF);
        } else {
            clear_map_words_for_behaviors(core.ram, 0x4E, 0xFD);
            write8(core.ram, kPlayerInteractionType1CLatch, 0xFF);
        }
        actor_clear_and_release(core, actor_slot);
        return actor_initialize_from_template(
            core, actor_slot, kPlayerTemplateDeathType84);
    }

    return false;
}

bool actor_collision_reinitialize_interaction(
    CoreRuntime& core,
    std::size_t source_slot,
    std::size_t receiving_slot
) {
    const std::uint8_t source_type = actor_read8(
        actor_view(core.ram, source_slot), kActorTypeOffset);
    const RamAddress template_address = source_type == 0x2D
        ? kActorTemplateType84 : kActorTemplateType2DInteraction;
    if (!actor_initialize_from_template(
            core, receiving_slot, template_address)) {
        return false;
    }
    if ((read8(core.ram, kFramePhaseCounter) & 0x02U) != 0) {
        actor_write8(actor_view(core.ram, receiving_slot),
                     kActorFacingXOffset, 0xFF);
    }
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
        return actor_collision_toggle_facing(core, source_slot);
    }

    if (dispatch.handler == kActorCollisionClearSource
        || dispatch.handler == kActorCollisionType14
        || dispatch.handler == kActorCollisionType2F) {
        actor_clear_type_and_release(core, source_slot);
        return actor_collision_reinitialize_interaction(
            core, source_slot, receiving_slot);
    }

    if (dispatch.handler == kActorCollisionType2B) {
        if (!actor_collision_reinitialize_interaction(
                core, source_slot, receiving_slot)) {
            return false;
        }
        const ActorView receiver = actor_view(core.ram, receiving_slot);
        write8(core.ram, kActorCollisionEventFlag, 0xFF);
        actor_write8(receiver, kActorTypeOffset, 0x84);
        actor_write32(receiver, kActorAnimationPcOffset, kActorAnimationType2B);
        actor_write8(receiver, kActorMovementFlagsOffset, 0x40);

        const auto child = actor_find_free_slot(
            core.ram, ActorAllocationPool::GameplayForward);
        if (child && actor_initialize_from_template(
                core, *child, kActorTemplateType8D)) {
            const ActorView child_actor = actor_view(core.ram, *child);
            actor_write16(child_actor, kActorXOffset,
                          actor_read16(receiver, kActorXOffset));
            actor_write16(child_actor, kActorYOffset,
                          actor_read16(receiver, kActorYOffset));
        }
        return true;
    }

    if (dispatch.handler == kActorCollisionType30) {
        actor_clear_type_and_release(core, source_slot);
        actor_clear_owned_resources(core, receiving_slot);
        if (!actor_initialize_from_template(
                core, receiving_slot, kActorTemplateCollisionResponse)) {
            return false;
        }
        actor_write32(actor_view(core.ram, receiving_slot),
                      kActorAnimationPcOffset, kActorAnimationType30);
        return true;
    }

    if (dispatch.handler == kActorCollisionType2D2E31) {
        const ActorView source = actor_view(core.ram, source_slot);
        const std::uint8_t source_type = actor_read8(
            source, kActorTypeOffset);
        if (source_type == 0x80) {
            actor_write8(source, kActorTypeOffset, 0x84);
            actor_write32(source, kActorAnimationPcOffset,
                          kActorAnimationType2D2E31);
            actor_write32(source, kActorMovementPcOffset, 0);
            actor_write8(source, kActorAnimationTimerOffset, 0);
        } else if (source_type != 0x82) {
            return false;
        }
        const ActorView receiver = actor_view(core.ram, receiving_slot);
        actor_write8(receiver, kActorTypeOffset, 0x84);
        actor_write8(receiver, kActorMovementFlagsOffset, 0x40);
        return true;
    }

    // Other handlers have table identity but their shared response/resource
    // mutations are added only as each recovered contract is ported.
    return false;
}

}  // namespace openaladdin::core
