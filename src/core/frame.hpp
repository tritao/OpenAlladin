#pragma once

#include "core/ram.hpp"
#include "core/rom.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace openaladdin::core {

struct CoreTrace;
struct CoreInput;

struct CoreRuntime {
    GenesisRam ram{};
    RomView rom{};
};

struct FrameService {
    std::uint8_t ordinal = 0;
    RamAddress call_site = 0;
    RamAddress rom_entry_pc = 0;
    const char* name = "";
};

constexpr std::size_t kFrameServiceCount = 37;

const std::array<FrameService, kFrameServiceCount>& frame_service_table();

void reset(CoreRuntime& core);

void bind_rom(CoreRuntime& core, RomView rom);

// This is the recovered Game_FrameUpdateLoop skeleton. It intentionally has
// no gameplay policy yet: the only state mutation is the ROM phase increment
// and the four real Player_PublishWorldCoordinates call boundaries.
void step_frame(
    CoreRuntime& core,
    std::uint64_t frame_number,
    std::string_view input_token,
    CoreTrace* trace = nullptr
);

void step_frame(
    CoreRuntime& core,
    std::uint64_t frame_number,
    const CoreInput& input,
    std::string_view input_token,
    CoreTrace* trace = nullptr
);

void player_publish_world_coordinates(
    CoreRuntime& core,
    CoreTrace* trace = nullptr
);

}  // namespace openaladdin::core
