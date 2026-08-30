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
    state.player.terrain_response_active = 0x42;
    state.camera.x = 0x100;
    state.camera.vertical_threshold = 0x234;

    GameRamView ram;
    ram.bind_state(state);
    assert(ram.read16(0xFF7DFA) == 0x1234);
    assert(ram.read16(0xFF7E02) == 0x1334);
    assert(ram.read16(0xFF7E58) == 0x9ABC);
    assert(ram.read8(0xFFF0BE) == 0x42);
    state.frame.phase = 0xA5;
    assert(ram.read8(0xFF7E28) == 0xA5);
    state.interaction_state.target_current = 0x12;
    state.interaction_state.response_current = 0x34;
    state.interaction_state.response_pending = 0x56;
    assert(ram.read8(0xFFF0EC) == 0x12);
    assert(ram.read8(0xFFEFFA) == 0x34);
    assert(ram.read8(0xFFEFFB) == 0x56);

    ram.set_write_tracking(true);
    ram.write16(0xFF7DFA, 0xFEDC);
    ram.write8(0xFFF0BE, 0x11);
    ram.write8(0xFF7E28, 0x12);
    ram.write8(0xFFF0EC, 0x21);
    ram.write8(0xFFEFFA, 0x43);
    ram.write8(0xFFEFFB, 0x65);
    assert(state.player.x == static_cast<std::int16_t>(0xFEDC));
    assert(state.player.animation_selector.response_active == 0x11);
    assert(state.frame.phase == 0x12);
    assert(state.interaction_state.target_current == 0x21);
    assert(state.interaction_state.response_current == 0x43);
    assert(state.interaction_state.response_pending == 0x65);
    std::uint8_t value = 0;
    assert(ram.take_write(0xFF7DFA, value) && value == 0xFE);
    assert(ram.take_write(0xFF7DFB, value) && value == 0xDC);
    assert(ram.take_write(0xFFF0BE, value) && value == 0x11);
    assert(ram.take_write(0xFF7E28, value) && value == 0x12);
    assert(ram.take_write(0xFFF0EC, value) && value == 0x21);
    assert(ram.take_write(0xFFEFFA, value) && value == 0x43);
    assert(ram.take_write(0xFFEFFB, value) && value == 0x65);
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

    ActorState actor;
    actor.type = 0x42;
    actor.x = 0x1234;
    actor.y = 0x5678;
    actor.movement_flags = 0x06;
    actor.runtime_field_07 = 0x17;
    actor.runtime_field_07_delay = 0x02;
    actor.facing_x_flip = 0xFF;
    actor.facing_y_flip = 0x80;
    actor.movement_pc = 0x00123456;
    actor.movement_loop_pc = 0x00654321;
    actor.movement_loop_timer = 0x09;
    actor.movement_word_18 = static_cast<std::int16_t>(0xFEDC);
    actor.movement_word_1a = static_cast<std::int16_t>(0x0123);
    actor.sprite_attribute = 0x4567;
    actor.frame_ptr = 0x001ED422;
    actor.animation_pc = 0x001223DA;
    actor.movement_return_pc = 0x00123400;
    actor.flags = 0x08;
    actor.interaction_state = 0x46;
    actor.movement_command_timer = 0x03;
    actor.animation_timer = 0x04;
    actor.linked_actor_slot = -1;

    std::array<std::uint8_t, 0x42> private_actor_bytes{};
    GameRamView actor_ram;
    actor_ram.bind_actor(actor, private_actor_bytes);
    assert(actor_ram.read8(0x00) == 0x42);
    assert(actor_ram.read16(0x02) == 0x1234);
    assert(actor_ram.read16(0x04) == 0x5678);
    assert(actor_ram.read32(0x0A) == 0x00123456);
    assert(actor_ram.read32(0x0E) == 0x00654321);
    assert(actor_ram.read16(0x18) == 0xFEDC);
    assert(actor_ram.read32(0x14) == 0x001ED422);
    assert(actor_ram.read32(0x20) == 0x001223DA);
    assert(actor_ram.read32(0x38) == 0x00123400);
    assert(actor_ram.read8(0x3C) == 0x08);
    assert(actor_ram.read8(0x3D) == 0x46);
    assert(actor_ram.read32(0x3E) == 0xFFFFFFFF);

    actor_ram.write16(0x02, 0xABCD);
    actor_ram.write32(0x0A, 0x00ABCDEF);
    actor_ram.write16(0x1A, 0xFEDC);
    actor_ram.write8(0x3D, 0x52);
    actor_ram.write32(0x3E, 0x00000007);
    actor_ram.write8(0x28, 0xA5);
    assert(actor.x == 0xABCD);
    assert(actor.movement_pc == 0x00ABCDEF);
    assert(actor.movement_word_1a == static_cast<std::int16_t>(0xFEDC));
    assert(actor.interaction_state == 0x52);
    assert(actor.linked_actor_slot == 7);
    assert(private_actor_bytes[0x28] == 0xA5);
    const auto actor_record = actor_ram.actor_record();
    assert(actor_record[0x02] == 0xAB);
    assert(actor_record[0x03] == 0xCD);
    assert(actor_record[0x28] == 0xA5);

    return 0;
}
