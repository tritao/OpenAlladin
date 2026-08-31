#pragma once

#include "core/frame.hpp"

#include <cstddef>
#include <cstdint>

namespace openaladdin::core {

struct CoreTrace;

// The level table is a ROM data contract, not a native level object. Each
// record is 0x42 bytes and is selected by the byte in SCENE_STATE.
constexpr std::size_t kLevelTableRomOffset = 0x00002C78;
constexpr std::size_t kLevelTableEntrySize = 0x42;
constexpr std::size_t kLevelTableCount = 13;

// Loads the fields published by Level_LoadFromSceneState (0x1AA484). The
// resource pointers are kept as ROM identities in RAM; decoding and VDP
// transfer remain host-facing work for a later boundary.
bool level_load_from_scene_state(
    CoreRuntime& core,
    CoreTrace* trace = nullptr
);

// This is the recovered Level_InvokeFrameCallback trampoline (0x1A8F04):
// load the callback identity from RAM and dispatch the recovered callback
// contract. Unknown callbacks remain observable but are intentionally no-op
// until their RAM-visible behavior is recovered.
void level_invoke_frame_callback(
    CoreRuntime& core,
    CoreTrace* trace = nullptr
);

// Level_InvokeExitCallback at 0x001AE1C2. The exit callback is selected
// directly from the active ROM level-table record; it is not mirrored into
// gameplay RAM.
void level_invoke_exit_callback(
    CoreRuntime& core,
    CoreTrace* trace = nullptr
);

}  // namespace openaladdin::core
