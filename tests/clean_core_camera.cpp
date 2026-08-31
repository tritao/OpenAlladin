#include "core/camera.hpp"
#include "core/trace.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

void write_rom16(std::vector<std::uint8_t>& rom, std::size_t offset,
                 std::uint16_t value) {
    rom[offset] = static_cast<std::uint8_t>(value >> 8);
    rom[offset + 1] = static_cast<std::uint8_t>(value);
}

void set_row_pointer(
    openaladdin::core::GenesisRam& ram,
    std::uint16_t row,
    openaladdin::core::RamAddress pointer
) {
    openaladdin::core::write32(
        ram,
        openaladdin::core::kTerrainRowPointerTable
            + static_cast<openaladdin::core::RamAddress>(row) * 4,
        pointer);
}

void clear_actor_types(openaladdin::core::GenesisRam& ram) {
    using namespace openaladdin::core;
    for (std::size_t slot = 0; slot < kActorSlotCount; ++slot) {
        actor_write8(actor_view(ram, slot), kActorTypeOffset, 0);
    }
}

}  // namespace

int main() {
    using namespace openaladdin::core;

    std::vector<std::uint8_t> rom(0x7000, 0);
    rom[0x2A52 + 0x10] = 2;
    rom[0x2A52 + 0x14] = 4;
    rom[0x2BA4 + 0x10] = 3;
    rom[0x2BA4 + 0x14] = 5;
    write_rom16(rom, 0x693E, static_cast<std::uint16_t>(-4));

    CoreRuntime core;
    bind_rom(core, RomView{rom.data(), rom.size()});
    reset(core);

    camera_select_scroll_delta_profile(
        core, CameraScrollDeltaProfile::Full);
    assert(read32(core.ram, kCameraScrollDataCursor) == 0x693E);
    assert(camera_consume_scroll_delta(core) == -4);
    assert(read32(core.ram, kCameraScrollDataCursor) == 0x6940);
    assert(read_i16(core.ram, kActorRenderXOffset) == -4);
    assert(camera_consume_scroll_delta(core) == 0);
    assert(read32(core.ram, kCameraScrollDataCursor) == 0x6940);
    camera_select_scroll_delta_profile(
        core, CameraScrollDeltaProfile::Reduced);
    assert(read32(core.ram, kCameraScrollDataCursor) == 0x6952);
    camera_select_scroll_delta_profile(
        core, CameraScrollDeltaProfile::Tail);
    assert(read32(core.ram, kCameraScrollDataCursor) == 0x695A);

    CoreTrace callback_trace;
    write16(core.ram, kCameraScrollRenderOffset, 5);
    write32(core.ram, kLevelCameraScrollCallback, 0x001AAA88);
    camera_invoke_level_scroll_callback(core, &callback_trace);
    assert(read16(core.ram, kCameraScrollRenderOffset) == 4);
    assert(callback_trace.camera_callback == 0x001AAA88);
    write32(core.ram, kLevelCameraScrollCallback, 0x001AAC14);
    camera_invoke_level_scroll_callback(core);
    assert(read16(core.ram, kCameraScrollRenderOffset) == 5);
    camera_select_scroll_delta_profile(
        core, CameraScrollDeltaProfile::Full);
    write8(core.ram, kFramePhaseCounter, 3);
    write32(core.ram, kLevelCameraScrollCallback, 0x001AB066);
    camera_invoke_level_scroll_callback(core);
    assert(read_i16(core.ram, kActorRenderYOffset) == 3);
    assert(read_i16(core.ram, kActorRenderXOffset) == -4);
    assert(read32(core.ram, kCameraScrollDataCursor) == 0x6940);

    write16(core.ram, kPlayerX, 0x0054);
    write16(core.ram, kPlayerY, 0x0054);
    write16(core.ram, kCameraHorizontalThreshold, 0x0064);
    write16(core.ram, kCameraVerticalThreshold, 0x0064);
    write16(core.ram, kCameraReferenceX, 0x0020);
    write16(core.ram, kCameraReferenceY, 0x0020);
    write16(core.ram, kLevelWidthPixels, 0x1000);
    write16(core.ram, kLevelHeightPixels, 0x1000);
    camera_update_follow(core);
    assert(read16(core.ram, kPlayerX) == 0x0056);
    assert(read16(core.ram, kPlayerY) == 0x0057);
    assert(read16(core.ram, kWorldCameraX) == 0xFFFE);
    assert(read16(core.ram, kWorldCameraY) == 0xFFFD);
    assert(read16(core.ram, kCameraScrollX) == 0xFFFE);
    assert(read16(core.ram, kCameraScrollY) == 0xFFFD);
    assert(read8(core.ram, kCameraScrollLeftPending) == 0xFF);
    assert(read8(core.ram, kCameraScrollUpPending) == 0xFF);

    write8(core.ram, kCameraScrollLeftPending, 0);
    write8(core.ram, kCameraScrollUpPending, 0);
    write16(core.ram, kPlayerX, 0x0078);
    write16(core.ram, kPlayerY, 0x0078);
    write16(core.ram, kCameraHorizontalThreshold, 0x0064);
    write16(core.ram, kCameraVerticalThreshold, 0x0064);
    write16(core.ram, kCameraReferenceX, 0x0020);
    write16(core.ram, kCameraReferenceY, 0x0020);
    write16(core.ram, kCameraScrollX, 0);
    write16(core.ram, kCameraScrollY, 0);
    write16(core.ram, kWorldCameraX, 0);
    write16(core.ram, kWorldCameraY, 0);
    camera_update_follow(core);
    assert(read16(core.ram, kPlayerX) == 0x0074);
    assert(read16(core.ram, kPlayerY) == 0x0073);
    assert(read16(core.ram, kWorldCameraX) == 4);
    assert(read16(core.ram, kWorldCameraY) == 5);
    assert(read16(core.ram, kCameraScrollX) == 4);
    assert(read16(core.ram, kCameraScrollY) == 5);
    assert(read8(core.ram, kCameraScrollRightPending) == 0xFF);
    assert(read8(core.ram, kCameraScrollDownPending) == 0xFF);

    const std::uint16_t row_base = 0x0020;
    constexpr RamAddress kRow = 0x00FF2200;
    clear_actor_types(core.ram);
    write16(core.ram, kInteractionRowStride, 2);
    write16(core.ram, kCameraReferenceX, 0x0040);
    write16(core.ram, kCameraReferenceY, 0x0020);
    write16(core.ram, kCameraScrollX, 0x0020);
    write16(core.ram, kCameraScrollY, 0x0000);
    set_row_pointer(core.ram, row_base, kRow);
    assert(camera_scroll_left_and_refill(core));
    assert(read16(core.ram, kCameraScrollX) == 0x0010);
    assert(read16(core.ram, kCameraReferenceX) == 0x0050);
    assert(read32(core.ram, kInteractionRowPointer) == kRow + 0x000A);
    assert(read16(core.ram, kInteractionHandlerX) == 0x0050);
    assert(read16(core.ram, kInteractionHandlerY) == 0x0020);

    reset(core);
    clear_actor_types(core.ram);
    write16(core.ram, kInteractionRowStride, 2);
    write16(core.ram, kCameraReferenceX, 0x0040);
    write16(core.ram, kCameraReferenceY, 0x0020);
    write16(core.ram, kCameraScrollX, 0x0020);
    write16(core.ram, kLevelWidthTiles, 0x0100);
    set_row_pointer(core.ram, row_base, kRow);
    assert(camera_scroll_right_and_refill(core));
    assert(read16(core.ram, kCameraScrollX) == 0x0010);
    assert(read16(core.ram, kCameraReferenceX) == 0x0050);
    assert(read32(core.ram, kInteractionRowPointer) == kRow + 0x0036);
    assert(read16(core.ram, kInteractionHandlerX) == 0x0050);

    reset(core);
    clear_actor_types(core.ram);
    write16(core.ram, kCameraReferenceX, 0x0040);
    write16(core.ram, kCameraReferenceY, 0x0020);
    write16(core.ram, kCameraScrollY, 0x0020);
    set_row_pointer(core.ram, 0x0120, kRow);
    assert(camera_scroll_down_and_refill(core));
    assert(read16(core.ram, kCameraScrollY) == 0x0010);
    assert(read16(core.ram, kCameraReferenceY) == 0x0030);
    assert(read32(core.ram, kInteractionRowPointer) == kRow + 0x0008);
    assert(read16(core.ram, kInteractionHandlerY) == 0x0030);

    reset(core);
    clear_actor_types(core.ram);
    write16(core.ram, kCameraReferenceX, 0x0040);
    write16(core.ram, kCameraReferenceY, 0x0020);
    write16(core.ram, kCameraScrollY, 0x0020);
    set_row_pointer(core.ram, 0x0030, kRow);
    assert(camera_scroll_up_and_refill(core));
    assert(read16(core.ram, kCameraScrollY) == 0x0010);
    assert(read16(core.ram, kCameraReferenceY) == 0x0030);
    assert(read32(core.ram, kInteractionRowPointer) == kRow + 0x0008);
    assert(read16(core.ram, kInteractionHandlerY) == 0x0030);

    reset(core);
    write16(core.ram, kCameraScrollX, 0x0020);
    write16(core.ram, kCameraReferenceX, 0x0040);
    write8(core.ram, kCameraScrollLeftPending, 0xFF);
    write8(core.ram, kCameraScrollApplyGate, 0xFF);
    camera_publish_scroll(core);
    assert(read8(core.ram, kCameraScrollLeftPending) == 0xFF);
    write8(core.ram, kCameraScrollApplyGate, 0);
    camera_publish_scroll(core);
    assert(read8(core.ram, kCameraScrollLeftPending) == 0);

    return 0;
}
