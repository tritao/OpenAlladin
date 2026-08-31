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

enum class InteractionRowProfile : std::uint8_t {
    A,
    ACore,
    B,
    BCore,
};

struct InteractionRowPassResult {
    std::size_t rows_visited = 0;
    std::size_t selector_count = 0;
    std::size_t spawn_count = 0;
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

InteractionRowPassResult interaction_process_rows(
    CoreRuntime& core,
    InteractionRowProfile profile,
    CoreTrace* trace = nullptr
);

InteractionRowPassResult interaction_process_rows_a(
    CoreRuntime& core,
    CoreTrace* trace = nullptr
);
InteractionRowPassResult interaction_process_rows_a_core(
    CoreRuntime& core,
    CoreTrace* trace = nullptr
);
InteractionRowPassResult interaction_process_rows_b(
    CoreRuntime& core,
    CoreTrace* trace = nullptr
);
InteractionRowPassResult interaction_process_rows_b_core(
    CoreRuntime& core,
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

// TerrainScene5RandomStep at 0x001B3032. The PRNG state remains in RAM;
// callers receive the low byte returned by the shared ROM helper.
std::uint8_t terrain_scene5_random_step(CoreRuntime& core);

}  // namespace openaladdin::core
