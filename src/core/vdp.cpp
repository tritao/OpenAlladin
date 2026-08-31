#include "core/vdp.hpp"

#include "core/ram.hpp"

#include <algorithm>

namespace openaladdin::core {
namespace {

constexpr RamAddress kVdpTileRowCommandTableSelected = 0x00FF8680;
constexpr RamAddress kVdpTileRowCommandTableMirror = 0x00FF8700;
constexpr RamAddress kVdpTileRowCommandTableAlternate = 0x00FF8780;
constexpr std::uint16_t kVdpPlaneC000 = 0xC000;
constexpr std::uint16_t kVdpPlaneE000 = 0xE000;

std::uint32_t encode_plane_command(std::uint16_t plane) {
    // This is the 68000 sequence ROL.L #2, LSR.W #2, ORI.W #$4000,
    // evaluated over the longword plane base used by the ROM.
    const std::uint32_t rotated = static_cast<std::uint32_t>(plane) << 2;
    return (rotated & 0xFFFF0000U)
        | ((rotated & 0x0000FFFFU) >> 2)
        | 0x00004000U;
}

void write_table_entry(
    GenesisRam& ram,
    RamAddress table,
    std::size_t index,
    std::uint32_t value
) {
    write32(ram, table + static_cast<RamAddress>(index * 4), value);
}

}  // namespace

void vdp_reset(GenesisVdp& vdp) {
    vdp.vram.fill(0);
    vdp.cram.fill(0);
    vdp.vsram.fill(0);
    vdp.registers.fill(0);
    vdp.control_latch = 0;
    vdp.data_latch = 0;
    vdp.tile_row_stride = 0x40;
    vdp.tile_write_count = 0;
}

void vdp_build_tile_row_command_tables(
    GenesisRam& ram,
    std::uint16_t row_stride
) {
    std::uint16_t selected_plane = kVdpPlaneC000;
    std::uint16_t alternate_plane = kVdpPlaneE000;
    if (read8(ram, kVdpTilePlaneOrder) != 0) {
        selected_plane = kVdpPlaneE000;
        alternate_plane = kVdpPlaneC000;
    }

    std::uint32_t selected = encode_plane_command(selected_plane);
    std::uint32_t alternate = encode_plane_command(alternate_plane);
    for (std::size_t index = 0; index < 32; ++index) {
        write_table_entry(ram, kVdpTileRowCommandTableSelected,
                          index, selected);
        write_table_entry(ram, kVdpTileRowCommandTableMirror,
                          index, selected);
        write_table_entry(ram, kVdpTileRowCommandTableAlternate,
                          index, alternate);
        selected = (selected & 0xFFFF0000U)
            | static_cast<std::uint16_t>(selected + row_stride);
        alternate = (alternate & 0xFFFF0000U)
            | static_cast<std::uint16_t>(alternate + row_stride);
    }
}

VdpTileWriteResult vdp_write_tile_word(
    const GenesisRam& ram,
    GenesisVdp& vdp,
    std::uint16_t tile_x,
    std::uint16_t tile_row,
    std::uint16_t tile_word
) {
    VdpTileWriteResult result;
    const RamAddress table_entry = kVdpTileRowCommandTableSelected
        + static_cast<RamAddress>(tile_row) * 4;
    const std::uint32_t row_command = read32(ram, table_entry);
    result.table_entry_present = row_command != 0;

    const std::uint16_t command_high = static_cast<std::uint16_t>(
        row_command >> 16);
    const std::uint16_t command_low = static_cast<std::uint16_t>(
        row_command);
    const std::uint16_t word_offset = static_cast<std::uint16_t>(tile_x * 2U);
    const std::uint16_t control_low = static_cast<std::uint16_t>(
        command_low | word_offset);
    result.control = (static_cast<std::uint32_t>(control_low) << 16)
        | command_high;
    result.data = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(read16(ram, kSceneResourceTileBase)
                                   | tile_word)
        + 0x87C0U);

    const std::uint16_t plane = read8(ram, kVdpTilePlaneOrder) == 0
        ? kVdpPlaneC000 : kVdpPlaneE000;
    result.vram_address = static_cast<std::uint16_t>(
        plane + static_cast<std::uint32_t>(tile_row) * vdp.tile_row_stride
        + word_offset);
    vdp.control_latch = result.control;
    vdp.data_latch = result.data;
    vdp.vram[result.vram_address] = static_cast<std::uint8_t>(result.data >> 8);
    vdp.vram[static_cast<std::uint16_t>(result.vram_address + 1)] =
        static_cast<std::uint8_t>(result.data);
    ++vdp.tile_write_count;
    return result;
}

void vdp_clear_vram_c000(GenesisVdp& vdp) {
    std::fill(vdp.vram.begin() + 0xC000, vdp.vram.begin() + 0xE000, 0);
}

}  // namespace openaladdin::core
