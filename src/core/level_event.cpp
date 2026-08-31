#include "core/level_event.hpp"

#include "core/ram.hpp"
#include "core/rom.hpp"
#include "core/trace.hpp"

namespace openaladdin::core {
namespace {

constexpr RamAddress kLevelEventDispatchEntry = 0x001B634E;
constexpr std::size_t kLevelEventCommandDispatchTable = 0x000020C0;
constexpr std::size_t kLevelEventRecordSize = 6;

}  // namespace

LevelEventDispatchResult level_event_dispatch_timed_command(
    CoreRuntime& core,
    CoreTrace* trace
) {
    LevelEventDispatchResult result;
    const RamAddress cursor = read32(core.ram, kLevelEventScriptCursor);
    const std::uint8_t delay = rom_read8(core.rom, cursor);
    if (delay == 0) return result;

    const std::uint8_t tick = static_cast<std::uint8_t>(
        read8(core.ram, kLevelEventTick) + 1U);
    write8(core.ram, kLevelEventTick, tick);
    if (delay >= tick) return result;

    result.dispatched = true;
    result.command = rom_read8(core.rom, cursor + 1);
    result.arg0 = rom_read16(core.rom, cursor + 2);
    result.arg1 = rom_read16(core.rom, cursor + 4);
    write32(core.ram, kLevelEventScriptCursor,
            cursor + kLevelEventRecordSize);
    write8(core.ram, kLevelEventTick, 0);

    // The ROM adds 0x1A to the command byte in an 8-bit register before
    // indexing the 26-entry table. E6 therefore selects entry zero.
    const std::uint8_t table_index = static_cast<std::uint8_t>(
        result.command + 0x1AU);
    result.handler = rom_read32(
        core.rom,
        kLevelEventCommandDispatchTable
            + static_cast<std::size_t>(table_index) * 4);

    if (trace != nullptr) {
        trace->level_event_dispatched = true;
        trace->level_event_command = result.command;
        trace->level_event_arg0 = result.arg0;
        trace->level_event_arg1 = result.arg1;
        trace->level_event_handler = result.handler;
    }
    return result;
}

}  // namespace openaladdin::core
