#include "level_event.hpp"

#include "game_state.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    std::vector<std::uint8_t> rom(0x40, 0);
    // delay, command, arg0, arg1; all words use Genesis big-endian order.
    rom[0x10] = 2;
    rom[0x11] = 0xE8;
    rom[0x12] = 0x12;
    rom[0x13] = 0x34;
    rom[0x14] = 0xAB;
    rom[0x15] = 0xCD;
    // A zero-delay record terminates the stream after the first dispatch.
    rom[0x16] = 0;

    openaladdin::GameState state;
    openaladdin::LevelEventVm vm;
    vm.bind_rom(rom);
    vm.start(openaladdin::RomAddress{0x10});

    std::vector<openaladdin::LevelEventCommand> events;
    openaladdin::LevelEventServices services{
        [&](const openaladdin::LevelEventCommand& event) {
            events.push_back(event);
        },
    };

    vm.update(state, services);
    assert(vm.active());
    assert(vm.tick() == 1);
    assert(events.empty());

    vm.update(state, services);
    assert(vm.active());
    assert(vm.tick() == 2);
    assert(events.empty());

    vm.restore(openaladdin::RomAddress{0x10}, 1);
    vm.update(state, services);
    assert(vm.active());
    assert(vm.tick() == 2);
    assert(events.empty());

    vm.update(state, services);
    assert(vm.active());
    assert(vm.cursor().value == 0x16);
    assert(vm.tick() == 0);
    assert(events.size() == 1);
    assert(events[0].delay == 2);
    assert(events[0].command == 0xE8);
    assert(events[0].arg0 == 0x1234);
    assert(events[0].arg1 == 0xABCD);

    vm.update(state, services);
    assert(!vm.active());
    assert(vm.cursor().value == 0);
    assert(events.size() == 1);

    // Starting a new stream resets both the elapsed counter and prior fault.
    rom[0x20] = 1;
    rom[0x21] = 0xFF;
    rom[0x22] = 0x00;
    rom[0x23] = 0x01;
    rom[0x24] = 0x00;
    rom[0x25] = 0x02;
    vm.start(openaladdin::RomAddress{0x20});
    vm.update(state, services);
    assert(events.size() == 1);
    vm.update(state, services);
    assert(events.size() == 2);
    assert(events[1].command == 0xFF);
    assert(vm.cursor().value == 0x26);

    rom[0x3F] = 1;
    vm.start(openaladdin::RomAddress{0x3F});
    vm.update(state, services);
    assert(vm.active());
    assert(!vm.faulted());
    vm.update(state, services);
    assert(!vm.active());
    assert(vm.faulted());

    return 0;
}
