#include "animation.hpp"
#include "game_state.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
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
    std::vector<std::uint8_t> rom(0x200000, 0);
    rom[0] = 0x00;
    rom[1] = 0x02;
    rom[2] = 0x00;
    rom[3] = 0x00;
    constexpr std::size_t sound_stream = 0x12000;
    rom[sound_stream + 2] = 0xF3;
    rom[sound_stream + 3] = 0x4C;

    constexpr std::size_t callback_frame_reference = 0x0200;
    constexpr std::size_t callback_stream = 0x12100;
    const auto add_callback_stream = [&](std::size_t stream, std::uint32_t callback) {
        write_u16(rom, stream, static_cast<std::uint16_t>(callback_frame_reference));
        rom[stream + 2] = 0xFB;
        rom[stream + 3] = 0;
        write_u32(rom, stream + 4, callback);
    };
    add_callback_stream(callback_stream + 0x000, 0x001ACB5A);
    add_callback_stream(callback_stream + 0x010, 0x001ACB62);
    add_callback_stream(callback_stream + 0x020, 0x001ACB6A);
    add_callback_stream(callback_stream + 0x030, 0x001ACB72);
    add_callback_stream(callback_stream + 0x040, 0x001ACB7A);
    add_callback_stream(callback_stream + 0x050, 0x001ACB82);
    add_callback_stream(callback_stream + 0x060, 0x001ACB8A);
    add_callback_stream(callback_stream + 0x070, 0x001ACB92);
    add_callback_stream(callback_stream + 0x080, 0x001ACC18);
    add_callback_stream(callback_stream + 0x090, 0x001ACC20);
    add_callback_stream(callback_stream + 0x0A0, 0x001ACC28);
    add_callback_stream(callback_stream + 0x0B0, 0x001ACC56);
    add_callback_stream(callback_stream + 0x0C0, 0x001ACC5E);
    add_callback_stream(callback_stream + 0x0D0, 0x001ACD02);
    add_callback_stream(callback_stream + 0x0E0, 0x001ACD5A);
    add_callback_stream(callback_stream + 0x100, 0x001ACD7E);
    add_callback_stream(callback_stream + 0x120, 0x001ACBD8);
    rom[callback_frame_reference] = 0;
    rom[callback_frame_reference + 1] = 0;
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

    // Player_ProcessInteractionState emits the fixed 0x31 event from its
    // common non-special convergence block, including the FFF0CC response
    // handoff. The native caller supplies the scene VDP gate separately from
    // the animation VM's selector bytes.
    PlayerAnimationVm interaction_vm;
    interaction_vm.load_rom(test_rom);
    AnimationContext interaction_context;
    GameState interaction_state;
    interaction_state.camera.vdp_update = 1;
    interaction_state.player.terrain_landing_state = 1;
    interaction_state.player.terrain_response_timer_state = 1;
    interaction_context.state = &interaction_state;
    assert(!interaction_vm.select_player_interaction_state(interaction_context));
    const auto interaction_requests = interaction_vm.take_sound_requests();
    assert(interaction_requests.size() == 1);
    assert(interaction_requests.front() == 0x31);

    interaction_state.player.terrain_response_timer_state = 1;
    interaction_state.camera.vdp_update = 0;
    assert(!interaction_vm.select_player_interaction_state(interaction_context));
    assert(interaction_vm.take_sound_requests().empty());

    // FB callback parameters are dispatched against the live actor record.
    // Verify the recovered flag families, including the older callback pair
    // used by the shared movement/animation streams.
    GameState callback_state;
    AnimationContext callback_context;
    callback_context.state = &callback_state;
    ActorState callback_actor;
    callback_actor.animation_timer = 0;
    const auto run_callback = [&](std::size_t offset) {
        callback_actor.animation_pc = static_cast<std::uint32_t>(callback_stream + offset);
        PlayerAnimationVm callback_vm;
        callback_vm.load_rom(test_rom);
        callback_vm.bind_state(callback_state);
        assert(!callback_vm.update_actor(callback_actor, callback_context));
    };

    run_callback(0x000);
    assert((callback_actor.flags & 0x10U) != 0);
    run_callback(0x010);
    assert((callback_actor.flags & 0x10U) == 0);
    run_callback(0x020);
    assert((callback_actor.movement_flags & 0x40U) != 0);
    run_callback(0x030);
    assert((callback_actor.movement_flags & 0x40U) == 0);
    run_callback(0x040);
    assert((callback_actor.runtime_field_07 & 0x20U) != 0);
    run_callback(0x050);
    assert((callback_actor.runtime_field_07 & 0x20U) == 0);
    run_callback(0x060);
    assert((callback_actor.movement_flags & 0x10U) != 0);
    run_callback(0x070);
    assert((callback_actor.movement_flags & 0x10U) == 0);
    run_callback(0x080);
    assert((callback_actor.flags & 0x20U) != 0);
    run_callback(0x090);
    assert((callback_actor.flags & 0x20U) == 0);
    run_callback(0x0A0);
    assert((callback_actor.movement_flags & 0x01U) != 0);
    run_callback(0x0B0);
    assert((callback_actor.movement_flags & 0x01U) == 0);

    callback_state.camera.vdp_update = 1;
    callback_state.random.value = 1;
    run_callback(0x0C0);
    assert(callback_state.random.value == 20);
    assert(callback_actor.animation_pc == callback_stream + 0x0C0 + 8);

    PlayerAnimationVm random_audio_vm;
    random_audio_vm.load_rom(test_rom);
    random_audio_vm.bind_state(callback_state);
    callback_actor.animation_pc = static_cast<std::uint32_t>(callback_stream + 0x0C0);
    callback_state.random.value = 1;
    assert(!random_audio_vm.update_actor(callback_actor, callback_context));
    const auto random_audio = random_audio_vm.take_sound_requests();
    assert(random_audio.size() == 1 && random_audio.front() == 0x5E);

    callback_state.random.value = 1;
    run_callback(0x0D0);
    assert(callback_state.random.value == 20);
    callback_state.random.value = 1;
    PlayerAnimationVm parity_audio_vm;
    parity_audio_vm.load_rom(test_rom);
    parity_audio_vm.bind_state(callback_state);
    callback_actor.animation_pc = static_cast<std::uint32_t>(callback_stream + 0x0D0);
    assert(!parity_audio_vm.update_actor(callback_actor, callback_context));
    const auto parity_audio = parity_audio_vm.take_sound_requests();
    assert(parity_audio.size() == 1 && parity_audio.front() == 0x40);

    callback_actor.movement_word_18 = 0x1234;
    callback_actor.movement_word_1a = 0x5678;
    callback_state.random.value = 1;
    run_callback(0x0E0);
    assert(callback_actor.movement_word_18 == static_cast<std::int16_t>(0xFD34));
    assert(callback_actor.movement_word_1a == static_cast<std::int16_t>(0xFC78));
    callback_actor.movement_word_18 = 0x1234;
    callback_actor.movement_word_1a = 0x5678;
    callback_state.random.value = 1;
    run_callback(0x100);
    assert(callback_actor.movement_word_18 == static_cast<std::int16_t>(0x0034));
    assert(callback_actor.movement_word_1a == static_cast<std::int16_t>(0xFC78));

    callback_state.actors[2].x = 0x1234;
    callback_state.actors[2].y = 0x5678;
    callback_actor.linked_actor_slot = 2;
    callback_actor.x = 1;
    callback_actor.y = 2;
    run_callback(0x120);
    assert(callback_actor.x == 0x1234);
    assert(callback_actor.y == 0x5678);

    std::remove(test_rom);
    return 0;
}
