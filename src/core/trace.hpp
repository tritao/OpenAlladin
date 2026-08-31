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
    RamAddress frame_callback = 0;
    RamAddress exit_callback = 0;
    std::uint16_t level_exit_vdp_control = 0;
    bool level_event_dispatched = false;
    std::uint8_t level_event_command = 0;
    std::uint16_t level_event_arg0 = 0;
    std::uint16_t level_event_arg1 = 0;
    RamAddress level_event_handler = 0;
    std::size_t level08_vdp_record_count = 0;
    std::uint32_t level08_vdp_last_control = 0;
    std::uint16_t level08_vdp_last_data = 0;
    bool scene_vdp_record_emitted = false;
    std::uint32_t scene_vdp_command_address = 0;
    std::array<std::uint16_t, 5> scene_vdp_words{};
    bool scene_resource_processed = false;
    std::uint8_t scene_resource_status = 0;
    std::uint8_t scene_resource_last_command = 0;
    RamAddress scene_resource_last_handler = 0;
    RamAddress scene_resource_cursor = 0;
    RamAddress scene_resource_stream_pointer = 0;
    RamAddress scene_resource_c000_source = 0;
    std::uint32_t scene_resource_last_vdp_control = 0;
    std::uint16_t scene_resource_last_vdp_data = 0;
    std::uint16_t scene_resource_last_vram_address = 0;
    std::uint16_t scene_resource_tile_x = 0;
    std::uint16_t scene_resource_tile_y = 0;
    std::uint16_t scene_resource_tile_base = 0;
    std::uint16_t scene_resource_last_tile_x = 0;
    std::uint16_t scene_resource_last_tile_y = 0;
    std::uint8_t scene_resource_last_tile_row = 0;
    std::size_t scene_resource_instruction_count = 0;
    std::size_t scene_resource_tile_write_count = 0;
    std::uint16_t scene_resource_service_frame_count = 0;
    bool scene_resource_c000_load_requested = false;
    bool scene_resource_frame_palette_prepare_requested = false;
    bool scene_resource_presentation_scratch_observed = false;
    bool scene_resource_actor_spawned = false;
    std::size_t scene_resource_actor_spawn_slot = 0;
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
