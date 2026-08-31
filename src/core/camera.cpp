#include "core/camera.hpp"

#include "core/interaction.hpp"
#include "core/ram.hpp"
#include "core/rom.hpp"

namespace openaladdin::core {
namespace {

constexpr std::size_t kHorizontalDampingTable = 0x00002A52;
constexpr std::size_t kVerticalDampingTable = 0x00002BA4;
constexpr RamAddress kScrollDeltaTable = 0x0000693E;

std::uint16_t add_word(std::uint16_t value, std::uint16_t delta) {
    return static_cast<std::uint16_t>(value + delta);
}

bool advance_scroll_axis(
    GenesisRam& ram,
    RamAddress scroll_address,
    RamAddress reference_address
) {
    const std::uint16_t scroll = read16(ram, scroll_address);
    const std::uint16_t adjusted = static_cast<std::uint16_t>(scroll + 0x0100U);
    if (adjusted < 0x0110U) {
        if (adjusted > 0x00F0U) return false;
        write16(ram, scroll_address, add_word(scroll, 0x0010));
        write16(ram, reference_address,
                static_cast<std::uint16_t>(read16(ram, reference_address) - 0x0010));
    } else {
        write16(ram, scroll_address,
                static_cast<std::uint16_t>(scroll - 0x0010));
        write16(ram, reference_address,
                static_cast<std::uint16_t>(read16(ram, reference_address) + 0x0010));
    }
    return true;
}

RamAddress terrain_row_pointer(
    const GenesisRam& ram,
    std::uint16_t reference_x,
    std::uint16_t reference_y,
    std::uint16_t y_offset,
    std::uint16_t x_offset
) {
    const std::uint16_t row = static_cast<std::uint16_t>(
        reference_y + y_offset) & 0xFFF0U;
    const RamAddress table_entry = kTerrainRowPointerTable
        + static_cast<RamAddress>(row) * 4;
    const RamAddress base = read32(ram, table_entry);
    return base + static_cast<RamAddress>(reference_x >> 4) * 2
        + x_offset;
}

bool publish_horizontal_row(
    CoreRuntime& core,
    bool core_profile,
    std::uint16_t x_offset
) {
    const std::uint16_t column = read16(core.ram, kCameraReferenceX) >> 4;
    if (!core_profile && column == 0) return false;
    if (core_profile) {
        const std::uint16_t limit = static_cast<std::uint16_t>(
            read16(core.ram, kLevelWidthTiles) - 0x0016U);
        if (column >= limit) return false;
    }
    write32(
        core.ram,
        kInteractionRowPointer,
        terrain_row_pointer(
            core.ram,
            read16(core.ram, kCameraReferenceX),
            read16(core.ram, kCameraReferenceY),
            0,
            x_offset));
    if (core_profile) {
        (void)interaction_process_rows_a_core(core);
    } else {
        (void)interaction_process_rows_a(core);
    }
    return true;
}

bool publish_vertical_row(CoreRuntime& core, bool core_profile) {
    write32(
        core.ram,
        kInteractionRowPointer,
        terrain_row_pointer(
            core.ram,
            read16(core.ram, kCameraReferenceX),
            read16(core.ram, kCameraReferenceY),
            core_profile ? 0x00F0 : 0,
            0));
    if (core_profile) {
        (void)interaction_process_rows_b_core(core);
    } else {
        (void)interaction_process_rows_b(core);
    }
    return true;
}

}  // namespace

void camera_select_scroll_delta_profile(
    GenesisRam& ram,
    CameraScrollDeltaProfile profile
) {
    const RamAddress cursor = profile == CameraScrollDeltaProfile::Full
        ? kScrollDeltaTable
        : profile == CameraScrollDeltaProfile::Reduced
            ? kScrollDeltaTable + 0x0014 : kScrollDeltaTable + 0x001C;
    write32(ram, kCameraScrollDataCursor, cursor);
}

void camera_select_scroll_delta_profile(
    CoreRuntime& core,
    CameraScrollDeltaProfile profile
) {
    camera_select_scroll_delta_profile(core.ram, profile);
}

std::int16_t camera_consume_scroll_delta(CoreRuntime& core) {
    const RamAddress cursor = read32(core.ram, kCameraScrollDataCursor);
    const std::int16_t delta = static_cast<std::int16_t>(
        rom_read16(core.rom, cursor));
    if (delta != 0) {
        write32(core.ram, kCameraScrollDataCursor, cursor + 2);
        write_i16(core.ram, kActorRenderXOffset, delta);
    }
    return delta;
}

void camera_update_follow(CoreRuntime& core) {
    GenesisRam& ram = core.ram;
    const std::uint8_t delay = read8(ram, kCameraUpdateDelay);
    if (delay != 0) {
        write8(ram, kCameraUpdateDelay, static_cast<std::uint8_t>(delay - 1));
        return;
    }
    if (read8(ram, kCameraSpecialMode) != 0) return;

    const std::uint16_t player_x = read16(ram, kPlayerX);
    const std::uint16_t threshold_x = read16(ram, kCameraHorizontalThreshold);
    const std::uint16_t difference_x = static_cast<std::uint16_t>(
        player_x - threshold_x);
    if (difference_x != 0) {
        const bool left = player_x < threshold_x;
        const std::uint16_t index = left
            ? static_cast<std::uint16_t>(0 - difference_x) : difference_x;
        const std::uint8_t delta = rom_read8(
            core.rom,
            (left ? kHorizontalDampingTable : kHorizontalDampingTable)
                + index);
        if (delta != 0) {
            if (left) {
                if (read16(ram, kCameraReferenceX) > 0x0010U) {
                    write16(ram, kPlayerX, add_word(player_x, delta));
                    write16(ram, kWorldCameraX,
                            static_cast<std::uint16_t>(
                                read16(ram, kWorldCameraX) - delta));
                    write16(ram, kCameraScrollX,
                            static_cast<std::uint16_t>(
                                read16(ram, kCameraScrollX) - delta));
                    write8(ram, kCameraScrollLeftPending, 0xFF);
                }
            } else {
                const std::uint16_t effective = static_cast<std::uint16_t>(
                    read16(ram, kCameraReferenceX)
                    + read16(ram, kCameraScrollX) + delta);
                const std::uint16_t limit = static_cast<std::uint16_t>(
                    read16(ram, kLevelWidthPixels) - 0x0161U);
                if (effective < limit) {
                    write16(ram, kPlayerX,
                            static_cast<std::uint16_t>(player_x - delta));
                    write16(ram, kWorldCameraX, add_word(
                        read16(ram, kWorldCameraX), delta));
                    write16(ram, kCameraScrollX, add_word(
                        read16(ram, kCameraScrollX), delta));
                    write8(ram, kCameraScrollRightPending, 0xFF);
                }
            }
        }
    }

    const std::uint16_t player_y = read16(ram, kPlayerY);
    const std::uint16_t threshold_y = read16(ram, kCameraVerticalThreshold);
    const std::uint16_t difference_y = static_cast<std::uint16_t>(
        player_y - threshold_y);
    if (difference_y == 0) return;
    if (player_y < threshold_y) {
        const std::uint16_t index = static_cast<std::uint16_t>(0 - difference_y);
        const std::uint16_t delta = rom_read8(
            core.rom, kVerticalDampingTable + index);
        if (read16(ram, kCameraReferenceY) > 0x0010U) {
            write16(ram, kPlayerY, add_word(player_y, delta));
            write16(ram, kWorldCameraY,
                    static_cast<std::uint16_t>(
                        read16(ram, kWorldCameraY) - delta));
            write16(ram, kCameraScrollY,
                    static_cast<std::uint16_t>(
                        read16(ram, kCameraScrollY) - delta));
            write8(ram, kCameraScrollUpPending, 0xFF);
        }
        return;
    }

    const std::uint16_t delta = rom_read8(
        core.rom, kVerticalDampingTable + difference_y);
    const std::uint16_t effective = static_cast<std::uint16_t>(
        read16(ram, kCameraReferenceY) + read16(ram, kCameraScrollY) + delta);
    const std::uint16_t limit = static_cast<std::uint16_t>(
        read16(ram, kLevelHeightPixels) - 0x00F1U);
    if (effective >= limit) return;
    write16(ram, kPlayerY, static_cast<std::uint16_t>(player_y - delta));
    write16(ram, kWorldCameraY, add_word(read16(ram, kWorldCameraY), delta));
    write16(ram, kCameraScrollY, add_word(read16(ram, kCameraScrollY), delta));
    write8(ram, kCameraScrollDownPending, 0xFF);
}

bool camera_scroll_left_and_refill(CoreRuntime& core, CoreTrace* trace) {
    (void) trace;
    if (!advance_scroll_axis(
            core.ram, kCameraScrollX, kCameraReferenceX)) return false;
    return publish_horizontal_row(core, false, 0);
}

bool camera_scroll_right_and_refill(CoreRuntime& core, CoreTrace* trace) {
    (void) trace;
    if (!advance_scroll_axis(
            core.ram, kCameraScrollX, kCameraReferenceX)) return false;
    return publish_horizontal_row(core, true, 0x002C);
}

bool camera_scroll_down_and_refill(CoreRuntime& core, CoreTrace* trace) {
    (void) trace;
    if (!advance_scroll_axis(
            core.ram, kCameraScrollY, kCameraReferenceY)) return false;
    return publish_vertical_row(core, true);
}

bool camera_scroll_up_and_refill(CoreRuntime& core, CoreTrace* trace) {
    (void) trace;
    if (!advance_scroll_axis(
            core.ram, kCameraScrollY, kCameraReferenceY)) return false;
    return publish_vertical_row(core, false);
}

void camera_publish_scroll(CoreRuntime& core, CoreTrace* trace) {
    (void) trace;
    if (read8(core.ram, kCameraScrollApplyGate) != 0) return;
    if (read8(core.ram, kCameraScrollLeftPending) != 0) {
        (void)camera_scroll_left_and_refill(core, trace);
        write8(core.ram, kCameraScrollLeftPending, 0);
    }
    if (read8(core.ram, kCameraScrollRightPending) != 0) {
        (void)camera_scroll_right_and_refill(core, trace);
        write8(core.ram, kCameraScrollRightPending, 0);
    }
    if (read8(core.ram, kCameraScrollUpPending) != 0) {
        (void)camera_scroll_up_and_refill(core, trace);
        write8(core.ram, kCameraScrollUpPending, 0);
    }
    if (read8(core.ram, kCameraScrollDownPending) != 0) {
        (void)camera_scroll_down_and_refill(core, trace);
        write8(core.ram, kCameraScrollDownPending, 0);
    }
}

}  // namespace openaladdin::core
