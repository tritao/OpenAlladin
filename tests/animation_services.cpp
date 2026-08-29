#include "animation.hpp"

#include "game_state.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <optional>
#include <vector>

namespace {

void write_u16(std::vector<std::uint8_t>& rom, std::size_t address, std::uint16_t value) {
    rom[address] = static_cast<std::uint8_t>(value >> 8);
    rom[address + 1] = static_cast<std::uint8_t>(value);
}

void write_u32(std::vector<std::uint8_t>& rom, std::size_t address, std::uint32_t value) {
    rom[address] = static_cast<std::uint8_t>(value >> 24);
    rom[address + 1] = static_cast<std::uint8_t>(value >> 16);
    rom[address + 2] = static_cast<std::uint8_t>(value >> 8);
    rom[address + 3] = static_cast<std::uint8_t>(value);
}

}  // namespace

int main() {
    using namespace openaladdin;

    constexpr std::size_t kFrameReference = 0x0100;
    constexpr std::size_t kF5Stream = 0x12000;
    constexpr std::size_t kF6Stream = 0x12100;
    constexpr std::size_t kTemplate = 0x180;
    constexpr char kRomPath[] = "/tmp/openaladdin-animation-services-test.bin";

    std::vector<std::uint8_t> rom(0x200000, 0);
    write_u32(rom, kFrameReference, 0x001ED422);
    rom[kTemplate] = 0x42;
    rom[kTemplate + 0x02] = 0x08;
    write_u32(rom, kTemplate + 0x06, 0x0011F6D4);
    write_u16(rom, kTemplate + 0x0A, 0x6000);
    write_u32(rom, kTemplate + 0x0C, 0x00122B58);
    rom[kTemplate + 0x10] = 0;

    write_u16(rom, kF5Stream, static_cast<std::uint16_t>(kFrameReference));
    rom[kF5Stream + 2] = 0xF5;
    rom[kF5Stream + 3] = 0;
    write_u32(rom, kF5Stream + 4, static_cast<std::uint32_t>(kTemplate));

    write_u16(rom, kF6Stream, static_cast<std::uint16_t>(kFrameReference));
    rom[kF6Stream + 2] = 0xF6;
    rom[kF6Stream + 3] = 0;

    {
        std::ofstream output(kRomPath, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(rom.data()),
            static_cast<std::streamsize>(rom.size())
        );
    }

    ActorSystem actors;
    actors.reset();
    ActorLifecycleSystem lifecycle(actors);
    lifecycle.bind_rom(rom);
    actors[1].type = 0x20;
    actors[1].x = 320;
    actors[1].y = 480;
    actors[1].animation_pc = static_cast<std::uint32_t>(kF5Stream);

    GameState game_state;
    AnimationContext context;
    context.state = &game_state;

    PlayerAnimationVm spawn_vm;
    spawn_vm.load_rom(kRomPath);
    std::optional<ActorIndex> spawned;
    AnimationServices spawn_services;
    spawn_services.source_actor = 1;
    spawn_services.spawn_f5 = [&](ActorIndex source, const F5Command& command) {
        spawned = lifecycle.spawn_f5(source, command);
        return spawned;
    };
    assert(!spawn_vm.update_actor(actors[1], context, &spawn_services));
    assert(spawned && *spawned == 3);
    assert(actors[3].type == 0x42);
    assert(actors[3].x == actors[1].x);
    assert(actors[3].y == actors[1].y);
    F5Command unhandled_command;
    assert(!spawn_vm.take_spawn_command(unhandled_command));

    actors[1].animation_pc = static_cast<std::uint32_t>(kF6Stream);
    actors[1].resource_count = 0;
    assert(lifecycle.install(1, actors[1]));
    PlayerAnimationVm retire_vm;
    retire_vm.load_rom(kRomPath);
    AnimationServices retire_services;
    retire_services.source_actor = 1;
    retire_services.retire_actor = [&](ActorIndex actor, std::uint8_t command_mode) {
        lifecycle.retire_from_vm(actor, command_mode);
    };
    assert(retire_vm.update_actor(actors[1], context, &retire_services));
    assert(actors[1].type == 0);
    assert(!actors.resource_allocation(1));

    std::remove(kRomPath);
    return 0;
}
