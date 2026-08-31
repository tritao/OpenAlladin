#pragma once

#include "core/frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string_view>

namespace openaladdin::core {

struct TraceWrite {
    RamAddress address = 0;
    std::uint8_t width = 0;
    std::uint32_t value = 0;
    const char* phase = "";
    RamAddress rom_entry_pc = 0;
};

struct CoreTrace {
    std::ostream* output = nullptr;
    std::array<FrameService, kFrameServiceCount + 1> phases{};
    std::size_t phase_count = 0;
    std::array<TraceWrite, 8> writes{};
    std::size_t write_count = 0;
    std::size_t collision_contact_count = 0;
    RamAddress collision_player_handler = 0;
    RamAddress collision_actor_handler = 0;
    std::uint8_t interaction_selector = 0;
    RamAddress interaction_handler = 0;
    std::uint16_t interaction_index = 0;
    std::size_t interaction_spawn_slot = 0;
    RamAddress camera_callback = 0;
    bool frame_atomic = false;
};

void trace_begin(
    CoreTrace& trace,
    std::ostream& output,
    const CoreRuntime* core = nullptr
);
void trace_phase(CoreTrace& trace, const FrameService& phase);
void trace_write(
    CoreTrace& trace,
    RamAddress address,
    std::uint8_t width,
    std::uint32_t value,
    const char* phase,
    RamAddress rom_entry_pc
);
void trace_state(
    CoreTrace& trace,
    const CoreRuntime& core,
    std::uint64_t frame_number,
    std::string_view input_token,
    bool atomic
);

}  // namespace openaladdin::core
