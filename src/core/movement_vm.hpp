#pragma once

#include "core/frame.hpp"
#include "core/vm_common.hpp"

#include <cstddef>

namespace openaladdin::core {

// Executes one recovered MovementVM actor stream directly against its RAM
// record. No actor snapshot or host lifecycle metadata is involved.
VmRunResult movement_vm_run_actor(CoreRuntime& core, std::size_t actor_slot);

// MovementVM_TickActors at 0x001ADE36. The pass marker is the recovered RAM
// domain selector used by shared actor-VM handlers.
void movement_vm_tick_actors(CoreRuntime& core);

}  // namespace openaladdin::core
