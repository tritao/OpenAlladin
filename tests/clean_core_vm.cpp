#include "core/animation_vm.hpp"
#include "core/movement_vm.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

void write_u16(std::vector<std::uint8_t>& rom, std::size_t offset,
               std::uint16_t value) {
    rom[offset] = static_cast<std::uint8_t>(value >> 8);
    rom[offset + 1] = static_cast<std::uint8_t>(value);
}

void write_u32(std::vector<std::uint8_t>& rom, std::size_t offset,
               std::uint32_t value) {
    rom[offset] = static_cast<std::uint8_t>(value >> 24);
    rom[offset + 1] = static_cast<std::uint8_t>(value >> 16);
    rom[offset + 2] = static_cast<std::uint8_t>(value >> 8);
    rom[offset + 3] = static_cast<std::uint8_t>(value);
}

}  // namespace

int main() {
    using namespace openaladdin::core;

    std::vector<std::uint8_t> rom(0x400, 0);
    CoreRuntime core;
    bind_rom(core, RomView{rom.data(), rom.size()});
    reset(core);

    // Movement 0x83 and Animation 0xED are the same encoded RAM-write
    // contract. Both forms below write the live actor record and absolute
    // work RAM without passing through a typed mirror.
    auto actor = actor_view(core.ram, 1);
    actor_write8(actor, kActorTypeOffset, 1);
    actor_write32(actor, kActorFramePointerOffset, 1);
    actor_write32(actor, kActorMovementPcOffset, 0x100);
    rom[0x100] = 0;
    rom[0x101] = 0;
    rom[0x102] = 0x83;
    rom[0x103] = 0x11;  // byte, actor-relative
    write_u16(rom, 0x104, 0x0007);
    write_u16(rom, 0x106, 0x00CC);
    rom[0x108] = 0x83;
    rom[0x109] = 0x01;  // byte, absolute
    write_u16(rom, 0x10A, 0x7E28);
    write_u16(rom, 0x10C, 0x00A5);

    const VmRunResult movement = movement_vm_run_actor(core, 1);
    assert(movement.completed);
    assert(actor_read8(actor, kActorRuntimeField07Offset) == 0xCC);
    assert(read8(core.ram, kFramePhaseCounter) == 0xA5);
    assert(actor_read32(actor, kActorMovementPcOffset) == 0x10E);

    // The actor pass writes its domain selector in RAM, and the animation
    // pass clears it before applying its own stream.
    write8(core.ram, kFramePhaseCounter, 0);
    movement_vm_tick_actors(core);
    assert(read8(core.ram, kActorVmMovementPass) == 0xFF);
    animation_vm_tick_actors(core);
    assert(read8(core.ram, kActorVmMovementPass) == 0);

    // Animation root: frame-reference pointer, one shared ED write, then a
    // non-command frame byte. The cursor is committed directly to +0x20.
    auto animation_actor = actor_view(core.ram, 2);
    actor_write8(animation_actor, kActorTypeOffset, 1);
    actor_write32(animation_actor, kActorAnimationPcOffset, 0x200);
    write_u16(rom, 0x200, 0x0030);
    write_u32(rom, 0x0030, 0x00123456);
    rom[0x202] = 0xED;
    rom[0x203] = 0x11;  // byte, actor-relative
    write_u16(rom, 0x204, 0x0007);
    write_u16(rom, 0x206, 0x00AA);
    rom[0x208] = 0x00;

    const VmRunResult animation = animation_vm_run_actor(core, 2);
    assert(animation.completed);
    assert(actor_read32(animation_actor, kActorFramePointerOffset) == 0x00123456);
    assert(actor_read8(animation_actor, kActorRuntimeField07Offset) == 0xAA);
    assert(actor_read32(animation_actor, kActorAnimationPcOffset) == 0x208);

    // F2 is the flag predicate, not a value compare. Its actor-relative set
    // branch lands at the target longword and preserves that exact cursor.
    actor_write32(animation_actor, kActorAnimationPcOffset, 0x220);
    write_u16(rom, 0x220, 0x0030);
    rom[0x222] = 0xF2;
    rom[0x223] = 0xC1;  // actor-relative, branch when bit 1 is set
    write_u16(rom, 0x224, 0x0007);
    write_u32(rom, 0x226, 0x0230);
    rom[0x230] = 0x00;
    const VmRunResult flag = animation_vm_run_actor(core, 2);
    assert(flag.completed);
    assert(actor_read32(animation_actor, kActorAnimationPcOffset) == 0x230);

    // A frame step services MovementVM before AnimationVM and exposes both
    // actor records in the same trace-visible RAM image.
    actor_write32(actor, kActorMovementPcOffset, 0);
    actor_write32(animation_actor, kActorAnimationPcOffset, 0);
    write8(core.ram, kFramePhaseCounter, 0);
    step_frame(core, 1, "none");
    assert(read8(core.ram, kFramePhaseCounter) == 1);
    assert(read8(core.ram, kActorVmMovementPass) == 0);

    return 0;
}
