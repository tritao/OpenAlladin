#pragma once

#include "core/frame.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace openaladdin::core {

enum class ActorAllocationPool : std::uint8_t {
    CommonForward,
    CommonReverse,
    GameplayForward,
    GameplayReverse,
    AuxiliaryForward,
    SceneResourceForward,
};

struct ActorSpawnOverrides {
    std::uint32_t animation_pc = 0;
    std::uint32_t movement_pc = 0;
    std::uint8_t type = 0;
    bool override_type = false;
};

// Actor allocation scans the actual type bytes in the RAM table. No culling
// or host provenance participates in slot ownership.
std::optional<std::size_t> actor_find_free_slot(
    const GenesisRam& ram,
    ActorAllocationPool pool
);

// Actor_InitializeFromTemplate at 0x001AE30A. This copies the compact ROM
// template into its documented record fields and clears the exact transient
// fields while preserving caller-owned coordinates/loop/return fields.
bool actor_initialize_from_template(
    CoreRuntime& core,
    std::size_t actor_slot,
    RamAddress template_address
);

// Actor_AllocateSpriteVRAM / Actor_ClearOwnedResources. Ownership lives in
// the RAM bitmap and the actor's +0x29/+0x2A/+0x2E fields.
bool actor_allocate_sprite_resources(CoreRuntime& core, std::size_t actor_slot);
void actor_clear_owned_resources(CoreRuntime& core, std::size_t actor_slot);

// Shared actor cleanup used by terminal VM paths and later collision helpers.
void actor_clear_type_and_release(CoreRuntime& core, std::size_t actor_slot);
void actor_clear_and_release(CoreRuntime& core, std::size_t actor_slot);

std::optional<std::size_t> actor_spawn_from_template(
    CoreRuntime& core,
    ActorAllocationPool pool,
    RamAddress template_address,
    std::uint16_t x,
    std::uint16_t y,
    const ActorSpawnOverrides& overrides = {}
);

}  // namespace openaladdin::core
