#include "movement.hpp"

#include "game_ram.hpp"
#include "game_state.hpp"

#include <array>
#include <cassert>
#include <vector>

int main() {
    std::vector<std::uint8_t> rom(0x400, 0);
    // One signed delta followed by a movement-facing command. The command
    // jumps to a second step root at 0x200.
    rom[0x100] = 3;
    rom[0x101] = static_cast<std::uint8_t>(-2);
    rom[0x102] = 0x81;
    rom[0x103] = 0;
    rom[0x104] = 0x80;
    rom[0x105] = 0;
    rom[0x106] = 0x00;
    rom[0x107] = 0x00;
    rom[0x108] = 0x02;
    rom[0x109] = 0x00;
    rom[0x200] = 1;
    rom[0x201] = 2;

    std::array<openaladdin::ActorState, 32> actors{};
    auto& actor = actors[1];
    actor.type = 1;
    actor.x = 100;
    actor.y = 200;
    actor.movement_pc = 0x100;
    actor.frame_ptr = 1;

    openaladdin::MovementVm vm;
    vm.tick(actors, openaladdin::MovementContext{rom, 0, 0, nullptr, {}});
    assert(actor.x == 103);
    assert(actor.y == 198);
    assert(actor.facing_x_flip == 0xFF);
    assert(actor.movement_pc == 0x200);

    vm.tick(actors, openaladdin::MovementContext{rom, 0, 0, nullptr, {}});
    assert(actor.x == 102);
    assert(actor.y == 200);

    // Movement opcode 0x83 is the shared ActorVM ED handler. Its byte form
    // consumes a word-sized stream operand, while the address mode selects
    // actor-relative or absolute Genesis RAM.
    std::vector<std::uint8_t> write_rom(0x300, 0);
    write_rom[0x100] = 0;
    write_rom[0x101] = 0;
    write_rom[0x102] = 0x83;
    write_rom[0x103] = 0x11; // byte, actor-relative
    write_rom[0x104] = 0x00;
    write_rom[0x105] = 0x07;
    write_rom[0x106] = 0x00;
    write_rom[0x107] = 0xCC;
    write_rom[0x108] = 0x83;
    write_rom[0x109] = 0x01; // byte, absolute
    write_rom[0x10A] = 0x7E;
    write_rom[0x10B] = 0x28; // FF7E28: typed phase byte
    write_rom[0x10C] = 0x00;
    write_rom[0x10D] = 0xA5;

    openaladdin::GameState state;
    openaladdin::GameRamView ram;
    ram.bind_state(state);
    auto& write_actor = state.actors[1];
    write_actor.type = 1;
    write_actor.movement_pc = 0x100;
    write_actor.frame_ptr = 1;
    vm.tick(
        state.actors,
        openaladdin::MovementContext{write_rom, 0, 0, nullptr, {}, &ram}
    );
    assert(write_actor.runtime_field_07 == 0xCC);
    assert(state.frame.phase == 0xA5);
    assert(ram.read8(0xFF7E28) == 0xA5);
    assert(write_actor.movement_pc == 0x10E);

    // Absolute untyped RAM is retained by the same view across VM ticks.
    write_rom[0x140] = 0;
    write_rom[0x141] = 0;
    write_rom[0x142] = 0x83;
    write_rom[0x143] = 0x04; // long, absolute
    write_rom[0x144] = 0x12;
    write_rom[0x145] = 0x34;
    write_rom[0x146] = 0xDE;
    write_rom[0x147] = 0xAD;
    write_rom[0x148] = 0xBE;
    write_rom[0x149] = 0xEF;
    auto& absolute_actor = state.actors[2];
    absolute_actor.type = 1;
    absolute_actor.movement_pc = 0x140;
    absolute_actor.frame_ptr = 1;
    vm.tick(
        state.actors,
        openaladdin::MovementContext{write_rom, 0, 0, nullptr, {}, &ram}
    );
    assert(ram.read32(0xFF1234) == 0xDEADBEEF);
    return 0;
}
