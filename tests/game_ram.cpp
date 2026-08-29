#include "game_ram.hpp"

#include "game_state.hpp"

#include <array>
#include <cassert>

int main() {
    using namespace openaladdin;

    GameState state;
    state.player.x = 0x1234;
    state.player.y = 0x5678;
    state.player.vx = static_cast<std::int16_t>(0x9ABC);
    state.player.animation_selector.response_active = 0x42;
    state.camera.x = 0x100;
    state.camera.vertical_threshold = 0x234;

    GameRamView ram;
    ram.bind_state(state);
    assert(ram.read16(0xFF7DFA) == 0x1234);
    assert(ram.read16(0xFF7E02) == 0x1334);
    assert(ram.read16(0xFF7E58) == 0x9ABC);
    assert(ram.read8(0xFFF0BE) == 0x42);

    ram.set_write_tracking(true);
    ram.write16(0xFF7DFA, 0xFEDC);
    ram.write8(0xFFF0BE, 0x11);
    assert(state.player.x == static_cast<std::int16_t>(0xFEDC));
    assert(state.player.animation_selector.response_active == 0x11);
    std::uint8_t value = 0;
    assert(ram.take_write(0xFF7DFA, value) && value == 0xFE);
    assert(ram.take_write(0xFF7DFB, value) && value == 0xDC);
    assert(ram.take_write(0xFFF0BE, value) && value == 0x11);
    assert(!ram.take_write(0xFFF0BE, value));

    ram.write8(0xFF1234, 0xA5);
    assert(ram.read8(0xFF1234) == 0xA5);
    std::array<std::uint8_t, 0x10000> legacy{};
    std::array<std::uint8_t, 0x10000> flags{};
    ram.copy_legacy_memory(legacy);
    ram.copy_legacy_write_flags(flags);
    assert(legacy[0x1234] == 0xA5);

    GameRamView restored;
    restored.bind_state(state);
    restored.restore_legacy_memory(legacy, flags);
    assert(restored.read8(0xFF1234) == 0xA5);
    // Typed addresses remain backed by state even when a legacy image also
    // contains an old mirrored byte at that address.
    legacy[0x7DFA] = 0;
    restored.restore_legacy_memory(legacy, flags);
    assert(restored.read16(0xFF7DFA) == 0xFEDC);

    return 0;
}
