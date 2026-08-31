#include "core/level.hpp"

#include "core/actor.hpp"
#include "core/interaction.hpp"
#include "core/level_event.hpp"
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
constexpr std::size_t kLevelExitCallbackOffset = 0x28;
constexpr std::size_t kLevelFrameCallbackOffset = 0x2C;
constexpr std::size_t kLevelWidthOffset = 0x30;
constexpr std::size_t kLevelHeightOffset = 0x32;
constexpr std::size_t kLevelCameraCallbackOffset = 0x34;

constexpr RamAddress kLevelCallback00 = 0x001B5B66;
constexpr RamAddress kLevelCallback01 = 0x001B5B4A;
constexpr RamAddress kLevelCallback02 = 0x001B5B94;
constexpr RamAddress kLevelCallback03 = 0x001B5B9A;
constexpr RamAddress kLevelCallback06 = 0x001B5D3A;
constexpr RamAddress kLevelCallback08 = 0x001B6066;
constexpr RamAddress kLevelCallback10 = 0x001B623A;

constexpr RamAddress kLevel08RotatingVdpTable = 0x000029E0;
constexpr RamAddress kLevel08Type84Template = 0x001B7EE0;
constexpr RamAddress kLevel08ExitCallback = 0x001B64D0;
constexpr RamAddress kLevel08ExitType60Template = 0x001B7DC8;

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

void level08_emit_rotating_vdp_record(
    CoreRuntime& core,
    CoreTrace* trace
) {
    const std::uint16_t record_offset = read16(
        core.ram, kLevel08VdpRecordOffset);
    const RamAddress record = kLevel08RotatingVdpTable + record_offset;
    const std::uint32_t control = rom_read32(core.rom, record);
    const std::uint16_t data = rom_read16(core.rom, record + 4);

    // The native presentation boundary will consume these semantic VDP
    // packets. The gameplay-visible contract here is the exact rotating RAM
    // cursor and the trace of the packet selected by the ROM.
    if (trace != nullptr) {
        ++trace->level08_vdp_record_count;
        trace->level08_vdp_last_control = control;
        trace->level08_vdp_last_data = data;
    }

    std::uint16_t next = static_cast<std::uint16_t>(record_offset + 6U);
    if (next >= 0x0060U) next = 0;
    write16(core.ram, kLevel08VdpRecordOffset, next);
}

void level08_spawn_random_type84(CoreRuntime& core, std::uint8_t random) {
    if (random >= 0x46U) return;
    const auto slot = actor_find_free_slot(
        core.ram, ActorAllocationPool::CommonForward);
    if (!slot || !actor_initialize_from_template(
            core, *slot, kLevel08Type84Template)) {
        return;
    }

    const ActorView actor = actor_view(core.ram, *slot);
    actor_write16(
        actor,
        kActorXOffset,
        static_cast<std::uint16_t>(read16(core.ram, kWorldCameraX) + 0x0140U));
    actor_write16(
        actor,
        kActorYOffset,
        static_cast<std::uint16_t>((random & 0x0FU) + 0x01C4U));
}

void invoke_level08_frame_callback(CoreRuntime& core, CoreTrace* trace) {
    // Level08_EnterRoutine first refreshes slot 1's camera-relative mirror.
    // Slot zero remains the player; slot one is an ordinary RAM actor record.
    const ActorView event_actor = actor_view(core.ram, 1);
    write16(
        core.ram,
        kPlayerX,
        static_cast<std::uint16_t>(
            actor_read16(event_actor, kActorXOffset)
            - read16(core.ram, kWorldCameraX)));
    actor_write16(
        event_actor,
        kActorYOffset,
        read16(core.ram, kPlayerWorldY));

    std::uint16_t low = static_cast<std::uint16_t>(
        read16(core.ram, kLevel08EventCounterLow) + 1U);
    if (low >= 0x0136U) {
        low = 0;
        write16(
            core.ram,
            kLevel08EventCounterHigh,
            static_cast<std::uint16_t>(
                read16(core.ram, kLevel08EventCounterHigh) + 1U));
    }
    write16(core.ram, kLevel08EventCounterLow, low);

    level08_emit_rotating_vdp_record(core, trace);
    level08_emit_rotating_vdp_record(core, trace);

    const std::uint8_t frame_phase = read8(core.ram, kFramePhaseCounter);
    if ((frame_phase & 0x3FU) == 0) {
        write16(
            core.ram,
            kLevel08VdpScrollOffset,
            static_cast<std::uint16_t>(
                read16(core.ram, kLevel08VdpScrollOffset) + 1U));
    }

    std::uint16_t phase = static_cast<std::uint16_t>(
        read16(core.ram, kLevel08EventPhase)
        + read16(core.ram, kLevel08EventCounterHigh));
    if (phase >= 0x00C0U) {
        phase = static_cast<std::uint16_t>(phase - 0x00C0U);
        write16(core.ram, kLevel08EventPhase, phase);
        (void)level08_event_dispatch_command(core, trace);
        level08_spawn_random_type84(
            core, terrain_scene5_random_step(core));
    } else {
        write16(core.ram, kLevel08EventPhase, phase);
    }
}

void invoke_level08_exit_callback(CoreRuntime& core, CoreTrace* trace) {
    // Level08_ExitRoutine's VDP control write and resource transfer are a
    // presentation boundary. Keep the selected control word observable while
    // applying every gameplay-visible RAM/actor publication in order.
    if (trace != nullptr) trace->level_exit_vdp_control = 0x8B02;
    write32(core.ram, kLevel08EventCommandCursor, 0x0000262F);
    write16(core.ram, kLevel08VdpScrollOffset, 0x0140);

    const ActorView event_actor = actor_view(core.ram, 1);
    if (actor_initialize_from_template(
            core, 1, kLevel08ExitType60Template)) {
        actor_write16(
            event_actor, kActorXOffset, read16(core.ram, kPlayerWorldX));
        actor_write16(
            event_actor, kActorYOffset, read16(core.ram, kPlayerWorldY));
    }
    write32(core.ram, kPlayerAnimationPc, 0x00122350);
    write16(core.ram, kLevel08EventCounterHigh, 6);
    write8(core.ram, kCameraScrollApplyGate, 0xFF);
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
    publish32(core, trace, kSceneResourceVdpStreamPtr,
              rom_read32(core.rom, offset + 0x14));
    publish16(core, trace, kSceneResourceVdpStreamEnd,
              rom_read16(core.rom, offset + 0x18));
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
    case kLevelCallback02:
        (void)level_event_dispatch_timed_command(core, trace);
        break;
    case kLevelCallback03:
        break;
    case kLevelCallback06:
        (void)level_event_dispatch_timed_command(core, trace);
        write8(core.ram, kPlayerTerrainBounceAnimationState, 0);
        break;
    case kLevelCallback08:
        invoke_level08_frame_callback(core, trace);
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

void level_invoke_exit_callback(CoreRuntime& core, CoreTrace* trace) {
    const std::uint8_t scene = read8(core.ram, kSceneState);
    if (scene >= kLevelTableCount || !rom_is_bound(core.rom)) return;

    const std::size_t offset = kLevelTableRomOffset
        + static_cast<std::size_t>(scene) * kLevelTableEntrySize;
    if (offset + kLevelTableEntrySize > core.rom.size) return;

    const RamAddress callback = rom_read32(
        core.rom, offset + kLevelExitCallbackOffset);
    if (trace != nullptr) trace->exit_callback = callback;

    switch (callback) {
    case kLevel08ExitCallback:
        invoke_level08_exit_callback(core, trace);
        break;
    default:
        // Other exit identities remain observable and will be ported with
        // their recovered transition/resource contracts.
        break;
    }
}

}  // namespace openaladdin::core
