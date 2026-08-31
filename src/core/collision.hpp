#pragma once

#include "core/frame.hpp"

#include <cstddef>
#include <cstdint>

namespace openaladdin::core {

constexpr RamAddress kPlayerCollisionHandlerTable = 0x001CBE;
constexpr std::size_t kPlayerCollisionHandlerCount = 0x7F;
constexpr RamAddress kActorCollisionHandlerTable = 0x001EBA;
constexpr std::size_t kActorCollisionHandlerCount = 0x32;

// The ROM stores direct 68000 pointers in both collision tables. A zero
// pointer is useful in ROM-less tests; a direct RTS body is also represented
// as a no-op even though its table entry is nonzero.
struct CollisionDispatch {
    std::uint8_t actor_type = 0;
    RamAddress handler = 0;
    bool in_range = false;
    bool no_op = true;
};

struct CollisionBox {
    bool valid = false;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct CollisionPassResult {
    std::size_t contact_count = 0;
    std::size_t source_slot = 0;
    std::size_t receiving_slot = 0;
    CollisionDispatch dispatch{};
    bool handler_applied = false;
};

CollisionDispatch player_collision_dispatch(
    const CoreRuntime& core,
    std::uint8_t actor_type
);

CollisionDispatch actor_collision_dispatch(
    const CoreRuntime& core,
    std::uint8_t receiving_type
);

CollisionBox collision_frame_box(
    const CoreRuntime& core,
    RamAddress frame_pointer,
    int origin_x,
    int origin_y,
    std::uint8_t facing_x_flip
);

bool collision_overlaps(const CollisionBox& first, const CollisionBox& second);

// Actor_PlayerCollisionPass scans common slots 1..24. It publishes the
// current type immediately before table dispatch, matching the ROM's
// transient latch contract. Handler bodies are added incrementally below
// this boundary.
CollisionPassResult player_collision_pass(CoreRuntime& core);

// Actor_ActorCollisionPass scans auxiliary source slots 25..31 against
// common receiving slots 1..24 and dispatches by receiving type.
CollisionPassResult actor_collision_pass(CoreRuntime& core);

// ActorCollision_ToggleFacing at 0x001AC60E delegates to the shared
// Actor_ToggleHorizontalFacing helper. The flag gate is part of the ROM
// contract: an already-marked actor is left unchanged.
bool actor_collision_toggle_facing(CoreRuntime& core, std::size_t actor_slot);

// Apply the small actor-collision family whose mutations are closed by the
// recovered RAM contracts. Unimplemented handler bodies deliberately remain
// no-ops at this boundary until their shared lifecycle contracts are ported.
bool actor_collision_apply(
    CoreRuntime& core,
    std::size_t source_slot,
    std::size_t receiving_slot
);

}  // namespace openaladdin::core
