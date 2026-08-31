#include "core/level.hpp"

#include "core/trace.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

void write_rom16(
    std::vector<std::uint8_t>& rom,
    std::size_t offset,
    std::uint16_t value
) {
    rom[offset] = static_cast<std::uint8_t>(value >> 8);
    rom[offset + 1] = static_cast<std::uint8_t>(value);
}

void write_rom32(
    std::vector<std::uint8_t>& rom,
    std::size_t offset,
    std::uint32_t value
) {
    write_rom16(rom, offset, static_cast<std::uint16_t>(value >> 16));
    write_rom16(rom, offset + 2, static_cast<std::uint16_t>(value));
}

}  // namespace

int main() {
    using namespace openaladdin::core;

    constexpr std::size_t kEntry = kLevelTableRomOffset + kLevelTableEntrySize;
    std::vector<std::uint8_t> rom(0x4000, 0);
    write_rom16(rom, kEntry + 0x00, 0x0200);
    write_rom16(rom, kEntry + 0x02, 0x0300);
    write_rom16(rom, kEntry + 0x04, 0x0400);
    write_rom16(rom, kEntry + 0x06, 0x0500);
    write_rom32(rom, kEntry + 0x24, 0x00123456);
    write_rom32(rom, kEntry + 0x2C, 0x001B5B4A);
    write_rom16(rom, kEntry + 0x30, 0x012C);
    write_rom16(rom, kEntry + 0x32, 0x002D);
    write_rom32(rom, kEntry + 0x34, 0x001AAA88);

    CoreRuntime core;
    bind_rom(core, RomView{rom.data(), rom.size()});
    reset(core);
    write8(core.ram, kSceneState, 1);

    CoreTrace load_trace;
    assert(level_load_from_scene_state(core, &load_trace));
    assert(read16(core.ram, kWorldCameraX) == 0x0200);
    assert(read16(core.ram, kWorldCameraY) == 0x0300);
    assert(read16(core.ram, kCameraReferenceX) == 0x0200);
    assert(read16(core.ram, kCameraReferenceY) == 0x0300);
    assert(read16(core.ram, kPlayerX) == 0x0400);
    assert(read16(core.ram, kPlayerY) == 0x0500);
    assert(read16(core.ram, kCameraHorizontalThreshold) == 0x0400);
    assert(read16(core.ram, kCameraVerticalThreshold) == 0x0500);
    assert(read32(core.ram, kLevelBackgroundBlockSource) == 0x00123456);
    assert(read32(core.ram, kLevelFrameCallback) == 0x001B5B4A);
    assert(read32(core.ram, kLevelCameraScrollCallback) == 0x001AAA88);
    assert(read16(core.ram, kLevelWidthTiles) == 0x012C);
    assert(read16(core.ram, kLevelHeightTiles) == 0x002D);
    assert(read16(core.ram, kLevelWidthPixels) == 0x12C0);
    assert(read16(core.ram, kLevelHeightPixels) == 0x02D0);
    assert(read16(core.ram, kInteractionRowStride) == 0x0258);
    assert(read32(core.ram, kCameraScrollDataCursor) == 0x695E);
    assert(load_trace.write_count != 0);

    CoreTrace callback_trace;
    write16(core.ram, kPlayerWorldX, 0x1288);
    write16(core.ram, kPlayerWorldY, 0x01D5);
    write8(core.ram, kSceneScriptCountdown, 0);
    write8(core.ram, kLevelTimer, 2);
    level_invoke_frame_callback(core, &callback_trace);
    assert(callback_trace.frame_callback == 0x001B5B4A);
    assert(read8(core.ram, kSceneScriptCountdown) == 0xFF);
    assert(read8(core.ram, kLevelTimer) == 1);

    write32(core.ram, kLevelFrameCallback, 0x001B623A);
    write16(core.ram, kPlayerWorldX, 0x0C4A);
    write16(core.ram, kPlayerWorldY, 0x0496);
    write8(core.ram, kSceneScriptCountdown, 0);
    level_invoke_frame_callback(core);
    assert(read8(core.ram, kSceneScriptCountdown) == 1);

    write32(core.ram, kLevelFrameCallback, 0x001B5D3A);
    write8(core.ram, kPlayerTerrainBounceAnimationState, 7);
    level_invoke_frame_callback(core);
    assert(read8(core.ram, kPlayerTerrainBounceAnimationState) == 0);

    write32(core.ram, kLevelFrameCallback, 0x00DEAD01);
    write8(core.ram, kSceneScriptCountdown, 0x44);
    level_invoke_frame_callback(core);
    assert(read8(core.ram, kSceneScriptCountdown) == 0x44);

    write8(core.ram, kSceneState, kLevelTableCount);
    assert(!level_load_from_scene_state(core));

    return 0;
}
