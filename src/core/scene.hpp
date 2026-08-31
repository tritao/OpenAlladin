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

}  // namespace openaladdin::core
