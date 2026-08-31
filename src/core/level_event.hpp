#pragma once

#include "core/frame.hpp"

#include <cstdint>

namespace openaladdin::core {

struct CoreTrace;

struct LevelEventDispatchResult {
    bool dispatched = false;
    std::uint8_t command = 0;
    std::uint16_t arg0 = 0;
    std::uint16_t arg1 = 0;
    RamAddress handler = 0;
};

// The recovered LevelEvent_DispatchTimedCommand (0x1B634E). The stream
// cursor and elapsed tick are RAM fields; event records remain ROM-owned.
// This boundary only frames and selects the handler. Handler effects are
// ported separately as their actor/scene contracts become available.
LevelEventDispatchResult level_event_dispatch_timed_command(
    CoreRuntime& core,
    CoreTrace* trace = nullptr
);

}  // namespace openaladdin::core
