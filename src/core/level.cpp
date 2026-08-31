#include "core/level.hpp"

#include "core/ram.hpp"
#include "core/rom.hpp"
#include "core/trace.hpp"

namespace openaladdin::core {
namespace {

constexpr RamAddress kLevelLoadEntry = 0x001AA484;
constexpr RamAddress kLevelFrameCallbackEntry = 0x001A8F04;

constexpr std::size_t kLevelCameraXOffset = 0x00;
constexpr std::size_t kLevelCameraYOffset = 0x02;
constexpr std::size_t kLevelPlayerXOffset = 0x04;
constexpr std::size_t kLevelPlayerYOffset = 0x06;
constexpr std::size_t kLevelBackgroundBlocksOffset = 0x24;
constexpr std::size_t kLevelFrameCallbackOffset = 0x2C;
constexpr std::size_t kLevelWidthOffset = 0x30;
constexpr std::size_t kLevelHeightOffset = 0x32;
constexpr std::size_t kLevelCameraCallbackOffset = 0x34;

constexpr RamAddress kLevelCallback00 = 0x001B5B66;
constexpr RamAddress kLevelCallback01 = 0x001B5B4A;
constexpr RamAddress kLevelCallback03 = 0x001B5B9A;
constexpr RamAddress kLevelCallback06 = 0x001B5D3A;
constexpr RamAddress kLevelCallback10 = 0x001B623A;

void publish16(
    CoreRuntime& core,
    CoreTrace* trace,
    RamAddress address,
    std::uint16_t value
) {
    write16(core.ram, address, value);
    if (trace != nullptr) {
        trace_write(*trace, address, 2, value,
                    "Level_LoadFromSceneState", kLevelLoadEntry);
    }
}

void publish32(
    CoreRuntime& core,
    CoreTrace* trace,
    RamAddress address,
    std::uint32_t value
) {
    write32(core.ram, address, value);
    if (trace != nullptr) {
        trace_write(*trace, address, 4, value,
                    "Level_LoadFromSceneState", kLevelLoadEntry);
    }
}

void decrement_level_timer(CoreRuntime& core) {
    const std::uint8_t timer = read8(core.ram, kLevelTimer);
    if (timer != 0) write8(core.ram, kLevelTimer, timer - 1);
}

}  // namespace

bool level_load_from_scene_state(CoreRuntime& core, CoreTrace* trace) {
    const std::uint8_t scene = read8(core.ram, kSceneState);
    if (scene >= kLevelTableCount) return false;

    const std::size_t offset = kLevelTableRomOffset
        + static_cast<std::size_t>(scene) * kLevelTableEntrySize;
    if (!rom_is_bound(core.rom)
        || offset + kLevelTableEntrySize > core.rom.size) {
        return false;
    }

    const std::uint16_t camera_x = rom_read16(
        core.rom, offset + kLevelCameraXOffset);
    const std::uint16_t camera_y = rom_read16(
        core.rom, offset + kLevelCameraYOffset);
    const std::uint16_t player_x = rom_read16(
        core.rom, offset + kLevelPlayerXOffset);
    const std::uint16_t player_y = rom_read16(
        core.rom, offset + kLevelPlayerYOffset);
    const std::uint16_t width = rom_read16(
        core.rom, offset + kLevelWidthOffset);
    const std::uint16_t height = rom_read16(
        core.rom, offset + kLevelHeightOffset);

    publish16(core, trace, kWorldCameraX, camera_x);
    publish16(core, trace, kCameraReferenceX, camera_x);
    publish16(core, trace, kPlayerX, player_x);
    publish16(core, trace, kCameraHorizontalThreshold, player_x);
    publish16(core, trace, kWorldCameraY, camera_y);
    publish16(core, trace, kCameraReferenceY, camera_y);
    publish16(core, trace, kPlayerY, player_y);
    publish16(core, trace, kCameraVerticalThreshold, player_y);
    publish32(core, trace, kLevelBackgroundBlockSource,
              rom_read32(core.rom, offset + kLevelBackgroundBlocksOffset));
    publish32(core, trace, kLevelFrameCallback,
              rom_read32(core.rom, offset + kLevelFrameCallbackOffset));
    publish32(core, trace, kLevelCameraScrollCallback,
              rom_read32(core.rom, offset + kLevelCameraCallbackOffset));
    publish16(core, trace, kLevelWidthTiles, width);
    publish16(core, trace, kLevelHeightTiles, height);
    publish16(core, trace, kLevelWidthPixels,
              static_cast<std::uint16_t>(width << 4));
    publish16(core, trace, kLevelHeightPixels,
              static_cast<std::uint16_t>(height << 4));
    publish16(core, trace, kInteractionRowStride,
              static_cast<std::uint16_t>(width << 1));

    // Level_LoadFromSceneState resets the scroll-delta stream to its aligned
    // start before the first callback consumes a level-specific profile.
    publish32(core, trace, kCameraScrollDataCursor, 0x0000695E);
    return true;
}

void level_invoke_frame_callback(CoreRuntime& core, CoreTrace* trace) {
    const RamAddress callback = read32(core.ram, kLevelFrameCallback);
    if (trace != nullptr) trace->frame_callback = callback;

    switch (callback) {
    case kLevelCallback00:
        decrement_level_timer(core);
        break;
    case kLevelCallback01:
        if (read16(core.ram, kPlayerWorldX) > 0x1287U
            && read16(core.ram, kPlayerWorldY) < 0x01D6U) {
            write8(core.ram, kSceneScriptCountdown, 0xFF);
        }
        decrement_level_timer(core);
        break;
    case kLevelCallback03:
        break;
    case kLevelCallback06:
        write8(core.ram, kPlayerTerrainBounceAnimationState, 0);
        break;
    case kLevelCallback10:
        if (read8(core.ram, kSceneScriptCountdown) == 0
            && read16(core.ram, kPlayerWorldX) > 0x0C49U
            && read16(core.ram, kPlayerWorldY) > 0x0495U) {
            write8(core.ram, kSceneScriptCountdown, 1);
        }
        break;
    default:
        // The callback identity remains in RAM and in the trace. More
        // involved level callbacks will be ported with their event contracts.
        break;
    }
}

}  // namespace openaladdin::core
