#pragma once

#include "core/frame.hpp"

namespace openaladdin::core {

struct CoreTrace;

enum class SceneResourceRunStatus : std::uint8_t {
    Finished,
    StatusChanged,
    BudgetExhausted,
    InvalidStream,
};

struct SceneResourceTileWrite {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint8_t tile_row = 0;
    std::uint16_t tile_base = 0;
    std::uint32_t vdp_control = 0;
    std::uint16_t vdp_data = 0;
    std::uint16_t vram_address = 0;
};

// Presentation effects leave the core through plain function pointers. The
// interpreter itself remains SDL-free and allocation-free; a native renderer
// can collect these writes without becoming gameplay state.
struct SceneResourceEffects {
    void* context = nullptr;
    void (*write_tile)(void*, const SceneResourceTileWrite&) = nullptr;
    void (*service_frame)(void*) = nullptr;
    void (*load_or_clear_c000)(void*, RamAddress) = nullptr;
    void (*prepare_frame_and_palette)(void*) = nullptr;
    void (*object_command_noop_hook)(void*) = nullptr;
};

struct SceneResourceRunResult {
    SceneResourceRunStatus status = SceneResourceRunStatus::InvalidStream;
    RamAddress cursor = 0;
    RamAddress stream_pointer = 0;
    RamAddress c000_source = 0;
    RamAddress last_handler = 0;
    std::uint8_t last_command = 0;
    std::uint32_t last_vdp_control = 0;
    std::uint16_t last_vdp_data = 0;
    std::uint16_t last_vram_address = 0;
    std::uint16_t tile_x = 0;
    std::uint16_t tile_y = 0;
    std::uint16_t tile_base = 0;
    std::uint16_t last_tile_x = 0;
    std::uint16_t last_tile_y = 0;
    std::uint8_t last_tile_row = 0;
    std::size_t instruction_count = 0;
    std::size_t tile_write_count = 0;
    std::uint16_t service_frame_count = 0;
    bool c000_load_requested = false;
    bool frame_palette_prepare_requested = false;
    bool presentation_scratch_observed = false;
    bool actor_spawned = false;
    std::size_t actor_spawn_slot = 0;
};

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

// SceneResource_ProcessCommandStream at 0x001B21F6. Cursor coordinates are
// interpreter-local 68000 registers; the active tile base and resource
// status remain authoritative RAM. Commands below 0x20 use the ROM dispatch
// table at 0x0049D8, while commands 0x20 and above emit one tile write.
SceneResourceRunResult scene_resource_process_command_stream(
    CoreRuntime& core,
    RamAddress stream,
    std::uint16_t initial_x = 0,
    std::uint16_t initial_y = 0,
    const SceneResourceEffects& effects = {},
    CoreTrace* trace = nullptr,
    std::size_t instruction_budget = 1'000'000
);

// SceneResource_ProcessCommandStreamWithPresentationScratch at 0x001B21E4.
// The wrapper owns the FFEFFC lifetime exactly: set it for the nested VM,
// then clear it before returning regardless of the VM result.
SceneResourceRunResult scene_resource_process_command_stream_with_presentation_scratch(
    CoreRuntime& core,
    RamAddress stream,
    std::uint16_t initial_x = 0,
    std::uint16_t initial_y = 0,
    const SceneResourceEffects& effects = {},
    CoreTrace* trace = nullptr,
    std::size_t instruction_budget = 1'000'000
);

// SceneTable_SelectNextState's table-selection tail at 0x001B3EDC. The
// five six-byte records are ROM-owned; only the selected pointer, state byte,
// and wrapping index are published into Genesis RAM.
void scene_table_select_next_state(
    CoreRuntime& core,
    CoreTrace* trace = nullptr
);

}  // namespace openaladdin::core
