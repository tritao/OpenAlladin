#include "core/animation_vm.hpp"

#include <cstdlib>

namespace openaladdin::core {
namespace {

constexpr std::uint32_t kAsciiCounterAddress = kWorkRamBase + 0xEFE0;

bool is_animation_opcode(std::uint8_t opcode) {
    return opcode >= 0xEA && opcode <= 0xFE;
}

RamAddress predicate_target_address(
    std::size_t actor_slot,
    std::uint8_t flags,
    std::uint16_t offset
) {
    return (flags & 0x80) != 0
        ? actor_address(actor_slot, offset)
        : kWorkRamBase + offset;
}

std::uint8_t advance_random(GenesisRam& ram) {
    const std::uint32_t state = read32(ram, kGlobalPrngState) * 13U + 7U;
    write32(ram, kGlobalPrngState, state);
    const std::uint16_t folded = static_cast<std::uint16_t>(
        (state & 0xFFFFU) ^ (state >> 16));
    return static_cast<std::uint8_t>(folded);
}

void clear_actor_record(GenesisRam& ram, std::size_t slot) {
    for (std::size_t offset = 0; offset < kActorRecordSize; ++offset) {
        write8(ram, actor_address(slot, offset), 0);
    }
}

void decrement_ascii_counter(GenesisRam& ram) {
    std::uint16_t digits = read16(ram, kAsciiCounterAddress);
    std::uint8_t high = static_cast<std::uint8_t>(digits >> 8);
    std::uint8_t low = static_cast<std::uint8_t>(digits);
    if (low == '0') {
        if (high == '0') return;
        high = static_cast<std::uint8_t>(high - 1);
        low = '9';
    } else {
        low = static_cast<std::uint8_t>(low - 1);
    }
    write16(ram, kAsciiCounterAddress,
            static_cast<std::uint16_t>((high << 8) | low));
}

void animation_callback(GenesisRam& ram, std::size_t slot, std::uint32_t callback) {
    const ActorView actor = actor_view(ram, slot);
    switch (callback) {
    case 0x001ACB5A:
        actor_write8(actor, kActorFlagsOffset,
                     static_cast<std::uint8_t>(actor_read8(actor, kActorFlagsOffset) | 0x10));
        break;
    case 0x001ACB62:
        actor_write8(actor, kActorFlagsOffset,
                     static_cast<std::uint8_t>(actor_read8(actor, kActorFlagsOffset) & ~0x10U));
        break;
    case 0x001ACB6A:
        actor_write8(actor, kActorMovementFlagsOffset,
                     static_cast<std::uint8_t>(actor_read8(actor, kActorMovementFlagsOffset) | 0x40));
        break;
    case 0x001ACB72:
        actor_write8(actor, kActorMovementFlagsOffset,
                     static_cast<std::uint8_t>(actor_read8(actor, kActorMovementFlagsOffset) & ~0x40U));
        break;
    case 0x001ACB7A:
        actor_write8(actor, kActorRuntimeField07Offset,
                     static_cast<std::uint8_t>(actor_read8(actor, kActorRuntimeField07Offset) | 0x20));
        break;
    case 0x001ACB82:
        actor_write8(actor, kActorRuntimeField07Offset,
                     static_cast<std::uint8_t>(actor_read8(actor, kActorRuntimeField07Offset) & ~0x20U));
        break;
    case 0x001ACB8A:
        actor_write8(actor, kActorMovementFlagsOffset,
                     static_cast<std::uint8_t>(actor_read8(actor, kActorMovementFlagsOffset) | 0x10));
        break;
    case 0x001ACB92:
        actor_write8(actor, kActorMovementFlagsOffset,
                     static_cast<std::uint8_t>(actor_read8(actor, kActorMovementFlagsOffset) & ~0x10U));
        break;
    case 0x001ACC18:
        actor_write8(actor, kActorFlagsOffset,
                     static_cast<std::uint8_t>(actor_read8(actor, kActorFlagsOffset) | 0x20));
        break;
    case 0x001ACC20:
        actor_write8(actor, kActorFlagsOffset,
                     static_cast<std::uint8_t>(actor_read8(actor, kActorFlagsOffset) & ~0x20U));
        break;
    case 0x001ACC28:
        actor_write8(actor, kActorMovementFlagsOffset,
                     static_cast<std::uint8_t>(actor_read8(actor, kActorMovementFlagsOffset) | 0x01));
        break;
    case 0x001ACC56:
        actor_write8(actor, kActorMovementFlagsOffset,
                     static_cast<std::uint8_t>(actor_read8(actor, kActorMovementFlagsOffset) & ~0x01U));
        break;
    case 0x001ACBD8: {
        const std::uint32_t linked_slot = actor_read32(
            actor, kActorLinkedSlotOffset);
        if (linked_slot < kActorSlotCount) {
            const ConstActorView linked = actor_view(
                static_cast<const GenesisRam&>(ram),
                static_cast<std::size_t>(linked_slot));
            actor_write16(actor, kActorXOffset,
                          actor_read16(linked, kActorXOffset));
            actor_write16(actor, kActorYOffset,
                          actor_read16(linked, kActorYOffset));
        }
        break;
    }
    case 0x001ACD5A:
        actor_write8(actor, kActorVelocityXOffset,
                     static_cast<std::uint8_t>(advance_random(ram) % 15U - 7U));
        actor_write8(actor, kActorVelocityYOffset,
                     static_cast<std::uint8_t>(advance_random(ram) % 15U - 15U));
        break;
    case 0x001ACD7E:
        actor_write8(actor, kActorVelocityXOffset,
                     static_cast<std::uint8_t>(advance_random(ram) % 7U - 4U));
        actor_write8(actor, kActorVelocityYOffset,
                     static_cast<std::uint8_t>(advance_random(ram) % 7U - 7U));
        break;
    case 0x001B0360:
        decrement_ascii_counter(ram);
        break;
    default:
        break;
    }
}

bool compare_branch(
    const GenesisRam& ram,
    RomView rom,
    std::size_t actor_slot,
    std::uint32_t& cursor,
    std::uint8_t flags
) {
    const std::uint8_t width = vm_value_width(flags);
    const RamAddress address = predicate_target_address(
        actor_slot, flags, rom_read16(rom, cursor + 2));
    cursor += 4;
    const std::uint32_t value = vm_read_encoded_value(rom, cursor, flags);
    const std::uint32_t current = width == 1
        ? read8(ram, address)
        : width == 2 ? read16(ram, address) : read32(ram, address);
    cursor += vm_encoded_operand_size(flags);
    const std::uint8_t operation = flags & 0x70;
    const bool branch = operation == 0x10 ? current == value
        : operation == 0x20 ? current != value
        : operation == 0x30 ? current >= value
                            : current < value;
    cursor = branch ? rom_read32(rom, cursor) : cursor + 4;
    return branch;
}

void flag_branch(
    const GenesisRam& ram,
    RomView rom,
    std::size_t actor_slot,
    std::uint32_t& cursor,
    std::uint8_t flags
) {
    const int bit = flags & 7;
    const RamAddress address = predicate_target_address(
        actor_slot, flags, rom_read16(rom, cursor + 2));
    const bool set = (read8(ram, address) & (1U << bit)) != 0;
    const bool branch = (flags & 0x40) != 0 ? set : !set;
    cursor = branch ? rom_read32(rom, cursor + 4) : cursor + 8;
}

}  // namespace

VmRunResult animation_vm_run_actor(CoreRuntime& core, std::size_t actor_slot) {
    VmRunResult result;
    if (!is_actor_slot(actor_slot) || !rom_is_bound(core.rom)) return result;

    const ActorView actor = actor_view(core.ram, actor_slot);
    if (actor_read8(actor, kActorTypeOffset) == 0) return result;
    const std::uint32_t stream_pc = actor_read32(
        actor, kActorAnimationPcOffset);
    if (stream_pc == 0 || stream_pc + 1 >= core.rom.size) return result;

    const std::uint16_t frame_reference = rom_read16(core.rom, stream_pc);
    actor_write32(actor, kActorFramePointerOffset,
                  rom_read32(core.rom, frame_reference));
    std::uint8_t timer = actor_read8(actor, kActorAnimationTimerOffset);
    if (timer != 0) {
        actor_write8(actor, kActorAnimationTimerOffset,
                     static_cast<std::uint8_t>(timer - 1));
        result.completed = true;
        result.cursor = stream_pc + 2;
        return result;
    }

    std::uint32_t cursor = stream_pc + 2;
    for (int command_index = 0; command_index < 1024; ++command_index) {
        const std::uint8_t opcode = rom_read8(core.rom, cursor);
        if (!is_animation_opcode(opcode)) {
            actor_write32(actor, kActorAnimationPcOffset, cursor);
            result.completed = true;
            result.cursor = cursor;
            return result;
        }
        ++result.command_count;
        result.blocked_opcode = opcode;
        switch (opcode) {
        case 0xEA:
            cursor = rom_read32(core.rom, cursor + 2);
            continue;
        case 0xEB:
            if (rom_read8(core.rom, cursor + 1) == 0) {
                actor_write8(actor, kActorFacingXOffset,
                    static_cast<std::uint8_t>(actor_read8(actor, kActorFacingXOffset) ^ 0xFF));
            } else {
                actor_write8(actor, kActorFacingYOffset,
                    static_cast<std::uint8_t>(actor_read8(actor, kActorFacingYOffset) ^ 0xFF));
            }
            cursor += 2;
            continue;
        case 0xEC:
            cursor += 2;
            if (rom_read8(core.rom, cursor - 1) != 0) {
                actor_write32(actor, kActorAnimationPcOffset, 0);
                result.stopped = true;
                result.completed = true;
                result.cursor = 0;
                return result;
            }
            continue;
        case 0xED:
            cursor = vm_apply_encoded_write(
                core.ram, core.rom, actor_slot, cursor);
            continue;
        case 0xEE:
            timer = rom_read8(core.rom, cursor + 1);
            cursor += 2;
            if ((timer & 0x80) != 0) {
                actor_write8(actor, kActorAnimationTimerOffset,
                             static_cast<std::uint8_t>(timer & 0x7F));
                actor_write32(actor, kActorAnimationPcOffset, cursor);
                result.stopped = true;
                result.completed = true;
                result.cursor = cursor;
                return result;
            }
            actor_write8(actor, kActorAnimationScratchOffset, timer);
            actor_write32(actor, kActorMovementReturnPcOffset, cursor);
            continue;
        case 0xEF: {
            cursor += 2;
            const std::uint8_t scratch = actor_read8(
                actor, kActorAnimationScratchOffset);
            if (scratch != 0) {
                actor_write8(actor, kActorAnimationScratchOffset,
                             static_cast<std::uint8_t>(scratch - 1));
                cursor = actor_read32(actor, kActorMovementReturnPcOffset);
            }
            continue;
        }
        case 0xF0: {
            const std::uint8_t threshold = rom_read8(core.rom, cursor + 1);
            const std::uint8_t random = advance_random(core.ram);
            cursor += 2;
            cursor = random < threshold
                ? rom_read32(core.rom, cursor) : cursor + 4;
            continue;
        }
        case 0xF1: {
            const std::uint8_t axis = rom_read8(core.rom, cursor + 1);
            const std::int16_t delta = static_cast<std::int16_t>(
                rom_read16(core.rom, cursor + 2));
            if (axis == 0) {
                actor_write16(actor, kActorXOffset, static_cast<std::uint16_t>(
                    static_cast<int>(actor_read16(actor, kActorXOffset))
                    + (actor_read8(actor, kActorFacingXOffset) != 0 ? -delta : delta)));
            } else {
                actor_write16(actor, kActorYOffset, static_cast<std::uint16_t>(
                    static_cast<int>(actor_read16(actor, kActorYOffset))
                    + (actor_read8(actor, kActorFacingYOffset) != 0 ? -delta : delta)));
            }
            cursor += 4;
            continue;
        }
        case 0xF2:
            flag_branch(
                core.ram, core.rom, actor_slot, cursor,
                rom_read8(core.rom, cursor + 1));
            continue;
        case 0xF3:
            cursor += 2;
            continue;
        case 0xF4:
            (void)compare_branch(
                core.ram, core.rom, actor_slot, cursor,
                rom_read8(core.rom, cursor + 1));
            continue;
        case 0xF5:
            cursor += 16;
            continue;
        case 0xF6:
            if (actor_slot != 0) {
                clear_actor_record(core.ram, actor_slot);
                result.stopped = true;
                result.completed = true;
                result.cursor = 0;
                return result;
            }
            cursor += 2;
            continue;
        case 0xF7:
            actor_write8(actor, kActorFacingXOffset, 0);
            if (read16(core.ram, kPlayerWorldX)
                < actor_read16(actor, kActorXOffset)) {
                actor_write8(actor, kActorFacingXOffset, 0xFF);
            }
            cursor += 2;
            continue;
        case 0xF8:
            // F8 enters the dynamic selector. The selector is a later player
            // routine; consuming the command preserves the VM boundary until
            // that recovered routine is ported.
            cursor += 2;
            continue;
        case 0xF9:
            actor_write8(actor, kActorVelocityYOffset,
                static_cast<std::uint8_t>(actor_read8(actor, kActorVelocityYOffset)
                    + rom_read8(core.rom, cursor + 1)));
            actor_write8(actor, kActorVelocityXOffset,
                static_cast<std::uint8_t>(actor_read8(actor, kActorVelocityXOffset)
                    + rom_read8(core.rom, cursor + 2)));
            cursor += 4;
            continue;
        case 0xFA: {
            const std::uint8_t mode = rom_read8(core.rom, cursor + 1);
            const std::uint16_t offset = rom_read16(core.rom, cursor + 2);
            const RamAddress address = (mode & 0x40) != 0
                ? actor_address(actor_slot, offset) : kWorkRamBase + offset;
            const bool subtract = (mode & 0x80) != 0;
            const std::uint8_t width = mode & 7;
            cursor += 4;
            if (width == 1) {
                const std::uint16_t value = rom_read16(core.rom, cursor);
                const std::uint8_t old = read8(core.ram, address);
                write8(core.ram, address, static_cast<std::uint8_t>(
                    subtract ? old - value : old + value));
                cursor += 2;
            } else if (width == 2) {
                const std::uint16_t value = rom_read16(core.rom, cursor);
                const std::uint16_t old = read16(core.ram, address);
                write16(core.ram, address, static_cast<std::uint16_t>(
                    subtract ? old - value : old + value));
                cursor += 2;
            } else {
                const std::uint32_t value = rom_read32(core.rom, cursor);
                const std::uint32_t old = read32(core.ram, address);
                write32(core.ram, address, subtract ? old - value : old + value);
                cursor += 4;
            }
            continue;
        }
        case 0xFB:
            animation_callback(core.ram, actor_slot,
                               rom_read32(core.rom, cursor + 2));
            cursor += 6;
            continue;
        case 0xFC:
            if ((rom_read8(core.rom, cursor + 1) & 0x80) != 0) {
                cursor = actor_read32(actor, kActorMovementReturnPcOffset);
            } else {
                actor_write32(actor, kActorMovementReturnPcOffset, cursor + 6);
                cursor = rom_read32(core.rom, cursor + 2);
            }
            continue;
        case 0xFD: {
            const int limit = rom_read8(core.rom, cursor + 1) == 0xFF
                ? 0x140 : rom_read8(core.rom, cursor + 1);
            const int distance = std::abs(
                static_cast<int>(read16(core.ram, kPlayerWorldX))
                - static_cast<int>(actor_read16(actor, kActorXOffset)));
            cursor += 2;
            cursor = limit >= distance ? rom_read32(core.rom, cursor) : cursor + 4;
            continue;
        }
        case 0xFE: {
            const int limit = rom_read8(core.rom, cursor + 1);
            const int distance = std::abs(
                static_cast<int>(read16(core.ram, kPlayerWorldY))
                - static_cast<int>(actor_read16(actor, kActorYOffset)));
            cursor += 2;
            cursor = limit >= distance ? rom_read32(core.rom, cursor) : cursor + 4;
            continue;
        }
        default:
            result.stopped = true;
            result.completed = true;
            result.cursor = cursor;
            actor_write32(actor, kActorAnimationPcOffset, cursor);
            return result;
        }
    }

    result.stopped = true;
    result.completed = true;
    result.cursor = cursor;
    actor_write32(actor, kActorAnimationPcOffset, cursor);
    return result;
}

void animation_vm_tick_actors(CoreRuntime& core) {
    if (!rom_is_bound(core.rom)) return;
    write8(core.ram, kActorVmMovementPass, 0);
    if ((read8(core.ram, kFramePhaseCounter) & 1) == 0) return;
    for (std::size_t slot = 0; slot < kActorSlotCount; ++slot) {
        (void)animation_vm_run_actor(core, slot);
    }
}

}  // namespace openaladdin::core
