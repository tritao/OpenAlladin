#pragma once

#include "core/frame.hpp"

namespace openaladdin::core {

struct CoreTrace;

// SceneScript_CompleteToState1 at 0x001B315C. The script cursor and pending
// byte are RAM contracts; the selected scene is then loaded from the ROM
// level table without a native scene object.
void scene_script_complete_to_state1(
    CoreRuntime& core,
    CoreTrace* trace = nullptr
);

// SceneResource_StreamVdpRecord at 0x001AE0F6. The stream cursor and command
// address latch are RAM-visible; VDP FIFO and Z80 handoff timing stay outside
// the gameplay core.
void scene_resource_stream_vdp_record(
    CoreRuntime& core,
    CoreTrace* trace = nullptr
);

// SceneTable_SelectNextState's table-selection tail at 0x001B3EDC. The
// five six-byte records are ROM-owned; only the selected pointer, state byte,
// and wrapping index are published into Genesis RAM.
void scene_table_select_next_state(
    CoreRuntime& core,
    CoreTrace* trace = nullptr
);

}  // namespace openaladdin::core
