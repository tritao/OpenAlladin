#include "core/level.hpp"

#include "core/level_event.hpp"

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

    // The timed VM increments its byte tick first and dispatches only when
    // delay < tick. E8 + 0x1A wraps to table entry two.
    const std::size_t stream = 0x0100;
    rom.resize(0x001B8200, 0);
    bind_rom(core, RomView{rom.data(), rom.size()});
    write_rom32(rom, 0x20C0 + 2 * 4, 0x001B766C);
    rom[0x001B81D8] = 0x7D;
    rom[stream + 0] = 2;
    rom[stream + 1] = 0xE8;
    write_rom16(rom, stream + 2, 0x1234);
    write_rom16(rom, stream + 4, 0xABCD);
    write32(core.ram, kLevelEventScriptCursor, stream);
    write8(core.ram, kLevelEventTick, 0);
    CoreTrace event_trace;
    assert(!level_event_dispatch_timed_command(core, &event_trace).dispatched);
    assert(read8(core.ram, kLevelEventTick) == 1);
    assert(!level_event_dispatch_timed_command(core, &event_trace).dispatched);
    assert(read8(core.ram, kLevelEventTick) == 2);
    const LevelEventDispatchResult event =
        level_event_dispatch_timed_command(core, &event_trace);
    assert(event.dispatched);
    assert(event.command == 0xE8);
    assert(event.arg0 == 0x1234);
    assert(event.arg1 == 0xABCD);
    assert(event.handler == 0x001B766C);
    assert(read32(core.ram, kLevelEventScriptCursor) == stream + 6);
    assert(read8(core.ram, kLevelEventTick) == 0);
    assert(event_trace.level_event_dispatched);
    assert(event_trace.level_event_handler == 0x001B766C);
    assert(actor_read8(actor_view(core.ram, 1), kActorTypeOffset) == 0x7D);
    assert(actor_read16(actor_view(core.ram, 1), kActorXOffset) == 0x1234);
    assert(actor_read16(actor_view(core.ram, 1), kActorYOffset) == 0xABCD);
    assert(actor_read16(actor_view(core.ram, 1), 0x1E) == 0x4000);
    assert(actor_read8(actor_view(core.ram, 1), kActorFacingXOffset) == 0xFF);

    // A zero-delay terminator is observed in place; the ROM helper does not
    // clear or advance the cursor when it reaches it.
    assert(!level_event_dispatch_timed_command(core).dispatched);
    assert(read32(core.ram, kLevelEventScriptCursor) == stream + 6);

    write32(core.ram, kLevelFrameCallback, 0x001B5B94);
    write32(core.ram, kLevelEventScriptCursor, stream);
    write8(core.ram, kLevelEventTick, 2);
    level_invoke_frame_callback(core, &event_trace);
    assert(read32(core.ram, kLevelEventScriptCursor) == stream + 6);

    // Level 08 owns a separate command/parameter stream and a rotating VDP
    // record cursor. Its callback also republishes the camera-relative slot
    // one record before folding the phase and dispatching one event.
    write_rom32(rom, 0x20C0 + 0x0D * 4, 0x001B5B32);
    write_rom32(rom, 0x29E0 + 0x54, 0x008B0200);
    write_rom16(rom, 0x29E0 + 0x58, 0x0011);
    write_rom32(rom, 0x29E0 + 0x5A, 0x008B0400);
    write_rom16(rom, 0x29E0 + 0x5E, 0x0022);
    rom[0x0200] = 0xF3;
    rom[0x0201] = 0x5A;
    write32(core.ram, kLevelFrameCallback, 0x001B6066);
    write16(core.ram, kWorldCameraX, 0x0100);
    write16(core.ram, kPlayerWorldY, 0x0300);
    actor_write16(actor_view(core.ram, 1), kActorXOffset, 0x0500);
    write16(core.ram, kLevel08EventPhase, 0x00BF);
    write16(core.ram, kLevel08EventCounterHigh, 1);
    write16(core.ram, kLevel08EventCounterLow, 0x0135);
    write16(core.ram, kLevel08VdpRecordOffset, 0x0054);
    write16(core.ram, kLevel08VdpScrollOffset, 0x0140);
    write32(core.ram, kLevel08EventCommandCursor, 0x0200);
    write32(core.ram, kGlobalPrngState, 29);
    write8(core.ram, kFramePhaseCounter, 0);
    CoreTrace level08_trace;
    level_invoke_frame_callback(core, &level08_trace);
    assert(level08_trace.frame_callback == 0x001B6066);
    assert(level08_trace.level_event_dispatched);
    assert(level08_trace.level_event_command == 0xF3);
    assert(level08_trace.level_event_arg1 == 0x5A);
    assert(level08_trace.level_event_handler == 0x001B5B32);
    assert(read16(core.ram, kPlayerX) == 0x0400);
    assert(actor_read16(actor_view(core.ram, 1), kActorYOffset) == 0x0300);
    assert(read16(core.ram, kLevel08EventCounterHigh) == 2);
    assert(read16(core.ram, kLevel08EventCounterLow) == 0);
    assert(read16(core.ram, kLevel08EventPhase) == 1);
    assert(read16(core.ram, kLevel08VdpRecordOffset) == 0);
    assert(read16(core.ram, kLevel08VdpScrollOffset) == 0x0141);
    assert(read32(core.ram, kLevel08EventCommandCursor) == 0x0202);
    assert(read8(core.ram, kLevelEventPresentationState) == 0x5A);
    assert(level08_trace.level08_vdp_record_count == 2);
    assert(level08_trace.level08_vdp_last_control == 0x008B0400);
    assert(level08_trace.level08_vdp_last_data == 0x0022);
    assert(read32(core.ram, kGlobalPrngState) == 384);

    // Level_InvokeExitCallback selects the callback from table offset 0x28.
    // Level 08 publishes its event stream and exit presentation actor through
    // the same RAM/actor records used by the running core.
    const std::size_t level08_entry = kLevelTableRomOffset
        + 8 * kLevelTableEntrySize;
    write_rom32(rom, level08_entry + 0x28, 0x001B64D0);
    rom[0x001B7DC8] = 0x60;
    write16(core.ram, kPlayerWorldX, 0x0600);
    write16(core.ram, kPlayerWorldY, 0x0220);
    write8(core.ram, kSceneState, 1);
    // The callback identity is selected by SCENE_STATE, so select level 08
    // for this direct exit-callback test.
    write8(core.ram, kSceneState, 8);
    CoreTrace exit_trace;
    level_invoke_exit_callback(core, &exit_trace);
    assert(exit_trace.exit_callback == 0x001B64D0);
    assert(exit_trace.level_exit_vdp_control == 0x8B02);
    assert(read32(core.ram, kLevel08EventCommandCursor) == 0x262F);
    assert(read16(core.ram, kLevel08VdpScrollOffset) == 0x0140);
    assert(actor_read8(actor_view(core.ram, 1), kActorTypeOffset) == 0x60);
    assert(actor_read16(actor_view(core.ram, 1), kActorXOffset) == 0x0600);
    assert(actor_read16(actor_view(core.ram, 1), kActorYOffset) == 0x0220);
    assert(read32(core.ram, kPlayerAnimationPc) == 0x00122350);
    assert(read16(core.ram, kLevel08EventCounterHigh) == 6);
    assert(read8(core.ram, kCameraScrollApplyGate) == 0xFF);

    return 0;
}
