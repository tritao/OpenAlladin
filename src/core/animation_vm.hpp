#pragma once

#include "core/frame.hpp"
#include "core/vm_common.hpp"

#include <cstddef>

namespace openaladdin::core {

// Executes one recovered AnimationVM stream directly against an actor record
// in Genesis RAM. Slot zero is the player actor, exactly as in the ROM table.
VmRunResult animation_vm_run_actor(CoreRuntime& core, std::size_t actor_slot);

// AnimationVM_TickActors at 0x001AC784. The pass is gated by the low bit of
// FRAME_PHASE_COUNTER and clears the shared VM-domain selector before service.
void animation_vm_tick_actors(CoreRuntime& core);

}  // namespace openaladdin::core
