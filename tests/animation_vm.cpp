#include "animation.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>

int main() {
    using namespace openaladdin;

    PlayerAnimationVm vm;
    vm.reset();
    assert(vm.pose() == SpritePose::Idle);
    assert(vm.sprite_frame() == 201);
    assert(vm.stream_entry() == 0x00121D9A);

    // Selecting a new pose starts its first frame without consuming a tick.
    vm.update(SpritePose::Run, HorizontalDirection::None);
    assert(vm.pose() == SpritePose::Run);
    assert(vm.sprite_frame() == 201);
    assert(vm.timer() == 4);
    vm.update(SpritePose::Run, HorizontalDirection::None);
    vm.update(SpritePose::Run, HorizontalDirection::None);
    vm.update(SpritePose::Run, HorizontalDirection::None);
    assert(vm.sprite_frame() == 201);
    vm.update(SpritePose::Run, HorizontalDirection::None);
    assert(vm.sprite_frame() == 202);

    vm.update(SpritePose::Run, HorizontalDirection::Left);
    assert(vm.facing_left());

    // Facing is retained while idle, but an exclusive right input must
    // immediately clear the horizontal flip for a left-to-right reversal.
    vm.update(SpritePose::Idle, HorizontalDirection::None);
    assert(vm.facing_left());
    vm.update(SpritePose::Run, HorizontalDirection::Right);
    assert(!vm.facing_left());

    vm.update(SpritePose::Jump, HorizontalDirection::None);
    assert(vm.pose() == SpritePose::Jump);
    assert(vm.sprite_frame() == 161);
    assert(vm.stream_entry() == 0x001221B0);
    vm.update(SpritePose::Jump, HorizontalDirection::None);
    vm.update(SpritePose::Jump, HorizontalDirection::None);
    vm.update(SpritePose::Jump, HorizontalDirection::None);
    vm.update(SpritePose::Jump, HorizontalDirection::None);
    assert(vm.sprite_frame() == 162);

    // The non-looping jump clip holds its last frame until physics selects a
    // grounded pose again.
    for (int i = 0; i < 40; ++i) {
        vm.update(SpritePose::Jump, HorizontalDirection::None);
    }
    assert(vm.sprite_frame() == 165);

    vm.update(SpritePose::Landing, HorizontalDirection::None);
    assert(vm.pose() == SpritePose::Landing);
    assert(vm.sprite_frame() == 171);
    for (int i = 0; i < 6; ++i) {
        vm.update(SpritePose::Landing, HorizontalDirection::None);
    }
    assert(vm.sprite_frame() == 161);

    vm.update(SpritePose::Idle, HorizontalDirection::None);
    assert(vm.pose() == SpritePose::Idle);
    assert(vm.sprite_frame() == 201);

    vm.update(SpritePose::Brake, HorizontalDirection::None);
    assert(vm.pose() == SpritePose::Brake);
    assert(vm.sprite_frame() == 233);
    for (int i = 0; i < 40; ++i) {
        vm.update(SpritePose::Brake, HorizontalDirection::None);
    }
    assert(vm.sprite_frame() == 319);

    // F3 carries a ROM sound-table ID. Verify that the native VM exposes it
    // to the engine instead of silently consuming the command.
    std::vector<std::uint8_t> rom(0x20010, 0);
    rom[0] = 0x00;
    rom[1] = 0x02;
    rom[2] = 0x00;
    rom[3] = 0x00;
    constexpr std::size_t sound_stream = 0x12000;
    rom[sound_stream + 2] = 0xF3;
    rom[sound_stream + 3] = 0x4C;
    const char* test_rom = "/tmp/openaladdin-animation-sound-test.bin";
    {
        std::ofstream output(test_rom, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(rom.data()),
            static_cast<std::streamsize>(rom.size())
        );
    }
    PlayerAnimationVm sound_vm;
    sound_vm.load_rom(test_rom);
    sound_vm.select_stream_entry(static_cast<std::uint32_t>(sound_stream));
    sound_vm.update(SpritePose::Idle, HorizontalDirection::None);
    const auto sound_requests = sound_vm.take_sound_requests();
    assert(sound_requests.size() == 1);
    assert(sound_requests.front() == 0x4C);
    assert(sound_vm.take_sound_requests().empty());
    std::remove(test_rom);
    return 0;
}
