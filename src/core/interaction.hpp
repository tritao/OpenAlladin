#pragma once

#include "core/actor.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace openaladdin::core {

struct CoreTrace;

constexpr RamAddress kInteractionHandlerTable = 0x004154;
constexpr std::size_t kInteractionHandlerCount = 0x100;

// The interaction table is a four-byte ROM pointer table indexed by the
// runtime selector byte. A zero entry is an ordinary unhandled selector.
struct InteractionDispatch {
    std::uint8_t selector = 0;
    RamAddress handler = 0;
    bool in_range = false;
    bool no_op = true;
};

struct InteractionSpawnResult {
    InteractionDispatch dispatch{};
    std::optional<std::size_t> actor_slot;
    bool handler_applied = false;
};

InteractionDispatch interaction_dispatch(
    const CoreRuntime& core,
    std::uint8_t selector
);

// Dispatch one already-selected interaction row entry. interaction_index is
// the row offset carried transiently by the ROM caller and recorded in the
// spawned actor's +0x32 field; it is not a second gameplay-state mirror.
InteractionSpawnResult interaction_spawn_dispatch(
    CoreRuntime& core,
    std::uint16_t interaction_index,
    std::uint8_t selector,
    CoreTrace* trace = nullptr
);

// The two shared ROM allocation contracts are public so later row processors
// and level callbacks can use the same helper without duplicating mutations.
std::optional<std::size_t> interaction_allocate_and_consume_row(
    CoreRuntime& core,
    ActorAllocationPool pool,
    RamAddress template_address,
    std::uint16_t interaction_index,
    std::uint8_t selector
);

std::optional<std::size_t> interaction_allocate_preserve_row(
    CoreRuntime& core,
    ActorAllocationPool pool,
    RamAddress template_address,
    std::uint16_t interaction_index,
    std::uint8_t selector
);

}  // namespace openaladdin::core
