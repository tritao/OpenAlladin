#include "movement.hpp"

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
    vm.tick(actors, openaladdin::MovementContext{rom, 0, 0});
    assert(actor.x == 103);
    assert(actor.y == 198);
    assert(actor.facing_x_flip == 0xFF);
    assert(actor.movement_pc == 0x200);

    vm.tick(actors, openaladdin::MovementContext{rom, 0, 0});
    assert(actor.x == 102);
    assert(actor.y == 200);
    return 0;
}
