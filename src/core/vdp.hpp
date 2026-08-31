#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace openaladdin::core {

struct GenesisRam;

constexpr std::size_t kVdpVramSize = 0x10000;
constexpr std::size_t kVdpCramWordCount = 0x40;
constexpr std::size_t kVdpVsramWordCount = 0x28;
constexpr std::size_t kVdpRegisterCount = 0x20;

// Semantic VDP state. The core does not emulate VDP bus instructions, but it
// owns the memories and latches that recovered gameplay/presentation helpers
// publish for the native renderer.
struct GenesisVdp {
    std::array<std::uint8_t, kVdpVramSize> vram{};
    std::array<std::uint16_t, kVdpCramWordCount> cram{};
    std::array<std::uint16_t, kVdpVsramWordCount> vsram{};
    std::array<std::uint8_t, kVdpRegisterCount> registers{};
    std::uint32_t control_latch = 0;
    std::uint16_t data_latch = 0;
    std::uint16_t tile_row_stride = 0x40;
    std::size_t tile_write_count = 0;
};

struct VdpTileWriteResult {
    bool table_entry_present = false;
    std::uint32_t control = 0;
    std::uint16_t data = 0;
    std::uint16_t vram_address = 0;
};

void vdp_reset(GenesisVdp& vdp);

// VDP_BuildTileRowCommandTables at 0x001B2142. The three generated tables
// live in Genesis RAM because VDP_WriteTileWord and camera refill routines
// consume those exact RAM addresses.
void vdp_build_tile_row_command_tables(
    GenesisRam& ram,
    std::uint16_t row_stride
);

// VDP_WriteTileWord at 0x001B21A8. D0 is the tile X word offset, D1 is the
// vertical tile-row command index, and D2 is the tile word ORed with the
// active scene-resource tile base.
VdpTileWriteResult vdp_write_tile_word(
    const GenesisRam& ram,
    GenesisVdp& vdp,
    std::uint16_t tile_x,
    std::uint16_t tile_y,
    std::uint16_t tile_word
);

void vdp_clear_vram_c000(GenesisVdp& vdp);

}  // namespace openaladdin::core
