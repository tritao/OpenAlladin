#include "core/movement_vm.hpp"

#include <cstdlib>

namespace openaladdin::core {
namespace {

constexpr std::uint32_t kDefaultActorMovementStream = 0x00121AD8;

bool is_movement_opcode(std::uint8_t opcode) {
    return opcode >= 0x80 && opcode <= 0x94;
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

std::int16_t signed_word(std::uint16_t value) {
    return static_cast<std::int16_t>(value);
}

void clear_actor_record(GenesisRam& ram, std::size_t slot) {
    for (std::size_t offset = 0; offset < kActorRecordSize; ++offset) {
        write8(ram, actor_address(slot, offset), 0);
    }
}

void integrate_actor(GenesisRam& ram, std::size_t slot) {
    const ActorView actor = actor_view(ram, slot);
    if (actor_read32(actor, kActorFramePointerOffset) == 0) return;

    std::int16_t velocity_y = read_i16(
        ram, actor_address(slot, kActorVelocityYOffset));
    if ((actor_read8(actor, kActorMovementFlagsOffset) & 0x40) != 0) {
        velocity_y = static_cast<std::int16_t>(velocity_y + 0x78);
        write_i16(ram, actor_address(slot, kActorVelocityYOffset), velocity_y);
    }

    std::int16_t velocity_x = read_i16(
        ram, actor_address(slot, kActorVelocityXOffset));
    const int x = static_cast<int>(actor_read16(actor, kActorXOffset))
        + (velocity_x >> 8);
    const int y = static_cast<int>(actor_read16(actor, kActorYOffset))
        + (velocity_y >> 8);
    actor_write16(actor, kActorXOffset, static_cast<std::uint16_t>(x));
    actor_write16(actor, kActorYOffset, static_cast<std::uint16_t>(y));

    const auto decay = [](std::int16_t value, std::int16_t step) {
        if (value < 0) {
            return value > static_cast<std::int16_t>(-step)
                ? static_cast<std::int16_t>(0)
                : static_cast<std::int16_t>(value + step);
        }
        return value < step
            ? static_cast<std::int16_t>(0)
            : static_cast<std::int16_t>(value - step);
    };
    write_i16(
        ram,
        actor_address(slot, kActorVelocityXOffset),
        decay(velocity_x, 0x28));
    write_i16(
        ram,
        actor_address(slot, kActorVelocityYOffset),
        decay(velocity_y, 0x3C));
}

void movement_callback(GenesisRam& ram, std::size_t slot, std::uint32_t callback) {
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
    case 0x001ACBD8: {
        const auto linked_slot = actor_slot_for_address(actor_read32(
            actor, kActorLinkedRecordPointerOffset));
        if (linked_slot) {
            const ConstActorView linked = actor_view(
                static_cast<const GenesisRam&>(ram),
                *linked_slot);
            actor_write16(actor, kActorXOffset,
                          actor_read16(linked, kActorXOffset));
            actor_write16(actor, kActorYOffset,
                          actor_read16(linked, kActorYOffset));
        }
        break;
    }
    default:
        break;
    }
}

}  // namespace

VmRunResult movement_vm_run_actor(CoreRuntime& core, std::size_t actor_slot) {
    VmRunResult result;
    if (!is_actor_slot(actor_slot) || !rom_is_bound(core.rom)) return result;

    const ActorView actor = actor_view(core.ram, actor_slot);
    if (actor_read8(actor, kActorTypeOffset) == 0) return result;
    const std::uint32_t step_pc = actor_read32(actor, kActorMovementPcOffset);
    if (step_pc == 0 || step_pc + 1 >= core.rom.size) return result;

    integrate_actor(core.ram, actor_slot);
    int delta_x = static_cast<std::int8_t>(rom_read8(core.rom, step_pc));
    int delta_y = static_cast<std::int8_t>(rom_read8(core.rom, step_pc + 1));
    if (actor_read8(actor, kActorFacingXOffset) != 0) delta_x = -delta_x;
    if (actor_read8(actor, kActorFacingYOffset) != 0) delta_y = -delta_y;
    actor_write16(actor, kActorXOffset, static_cast<std::uint16_t>(
        static_cast<int>(actor_read16(actor, kActorXOffset)) + delta_x));
    actor_write16(actor, kActorYOffset, static_cast<std::uint16_t>(
        static_cast<int>(actor_read16(actor, kActorYOffset)) + delta_y));

    std::uint32_t cursor = step_pc + 2;
    const std::uint8_t command_timer = actor_read8(
        actor, kActorMovementCommandTimerOffset);
    if (command_timer != 0) {
        actor_write8(actor, kActorMovementCommandTimerOffset,
                     static_cast<std::uint8_t>(command_timer - 1));
        result.completed = true;
        result.cursor = cursor;
        return result;
    }

    for (int command_index = 0; command_index < 32; ++command_index) {
        const std::uint8_t opcode = rom_read8(core.rom, cursor);
        if (!is_movement_opcode(opcode)) {
            result.completed = true;
            break;
        }
        ++result.command_count;
        result.blocked_opcode = opcode;
        switch (opcode) {
        case 0x80:
            cursor = rom_read32(core.rom, cursor + 2);
            if (!is_movement_opcode(rom_read8(core.rom, cursor))) {
                result.stopped = true;
                result.completed = true;
                break;
            }
            continue;
        case 0x81:
            if (rom_read8(core.rom, cursor + 1) != 0) {
                actor_write8(actor, kActorFacingYOffset,
                    static_cast<std::uint8_t>(actor_read8(actor, kActorFacingYOffset) ^ 0xFF));
            } else {
                actor_write8(actor, kActorFacingXOffset,
                    static_cast<std::uint8_t>(actor_read8(actor, kActorFacingXOffset) ^ 0xFF));
            }
            cursor += 2;
            continue;
        case 0x82:
            if (rom_read8(core.rom, cursor + 1) == 0) {
                actor_write32(actor, kActorMovementPcOffset, 0);
                result.stopped = true;
                result.completed = true;
                result.cursor = 0;
                return result;
            }
            actor_write32(actor, kActorAnimationPcOffset, 0);
            cursor += 2;
            continue;
        case 0x83:
            cursor = vm_apply_encoded_write(
                core.ram, core.rom, actor_slot, cursor);
            continue;
        case 0x84: {
            const std::uint8_t value = rom_read8(core.rom, cursor + 1);
            if ((value & 0x80) != 0) {
                actor_write8(actor, kActorMovementCommandTimerOffset,
                             static_cast<std::uint8_t>(value & 0x7F));
                actor_write32(actor, kActorMovementPcOffset, cursor + 2);
                result.stopped = true;
                result.completed = true;
                cursor += 2;
                break;
            }
            actor_write8(actor, kActorMovementLoopTimerOffset, value);
            actor_write32(actor, kActorMovementLoopPcOffset, cursor + 2);
            cursor += 2;
            continue;
        }
        case 0x85: {
            const std::uint8_t timer = actor_read8(
                actor, kActorMovementLoopTimerOffset);
            if (timer != 0) {
                actor_write8(actor, kActorMovementLoopTimerOffset,
                             static_cast<std::uint8_t>(timer - 1));
                cursor = actor_read32(actor, kActorMovementLoopPcOffset);
            } else {
                cursor += 2;
            }
            continue;
        }
        case 0x87: {
            const std::int16_t delta = signed_word(rom_read16(core.rom, cursor + 2));
            if (rom_read8(core.rom, cursor + 1) == 0) {
                const int mirrored = actor_read8(actor, kActorFacingXOffset) != 0
                    ? -delta : delta;
                actor_write16(actor, kActorXOffset, static_cast<std::uint16_t>(
                    static_cast<int>(actor_read16(actor, kActorXOffset)) + mirrored));
            } else {
                const int mirrored = actor_read8(actor, kActorFacingYOffset) != 0
                    ? -delta : delta;
                actor_write16(actor, kActorYOffset, static_cast<std::uint16_t>(
                    static_cast<int>(actor_read16(actor, kActorYOffset)) + mirrored));
            }
            cursor += 4;
            continue;
        }
        case 0x88: {
            const std::uint8_t flags = rom_read8(core.rom, cursor + 1);
            const int bit = flags & 7;
            const RamAddress address = predicate_target_address(
                actor_slot, flags, rom_read16(core.rom, cursor + 2));
            const bool set = (read8(core.ram, address) & (1U << bit)) != 0;
            const bool branch = (flags & 0x40) != 0 ? set : !set;
            cursor = branch ? rom_read32(core.rom, cursor + 4) : cursor + 8;
            continue;
        }
        case 0x8A: {
            const std::uint8_t flags = rom_read8(core.rom, cursor + 1);
            const std::uint8_t width = vm_value_width(flags);
            const RamAddress address = predicate_target_address(
                actor_slot, flags, rom_read16(core.rom, cursor + 2));
            cursor += 4;
            const std::uint32_t value = vm_read_encoded_value(
                core.rom, cursor, flags);
            const std::uint32_t current = width == 1
                ? read8(core.ram, address)
                : width == 2 ? read16(core.ram, address)
                             : read32(core.ram, address);
            cursor += vm_encoded_operand_size(flags);
            const std::uint8_t operation = flags & 0x70;
            const bool branch = operation == 0x10 ? current == value
                : operation == 0x20 ? current != value
                : operation == 0x30 ? current >= value
                                    : current < value;
            cursor = branch ? rom_read32(core.rom, cursor) : cursor + 4;
            continue;
        }
        case 0x8C:
            clear_actor_record(core.ram, actor_slot);
            result.stopped = true;
            result.completed = true;
            result.cursor = 0;
            return result;
        case 0x8D:
            actor_write8(actor, kActorFacingXOffset,
                read16(core.ram, kPlayerWorldX) < actor_read16(actor, kActorXOffset)
                    ? 0xFF : 0);
            cursor += 2;
            continue;
        case 0x8E:
            actor_write32(actor, kActorMovementPcOffset,
                          kDefaultActorMovementStream);
            result.stopped = true;
            result.completed = true;
            result.cursor = kDefaultActorMovementStream;
            return result;
        case 0x90: {
            const std::uint8_t mode = rom_read8(core.rom, cursor + 1);
            const std::uint16_t offset = rom_read16(core.rom, cursor + 2);
            const std::int16_t value = signed_word(rom_read16(core.rom, cursor + 4));
            const RamAddress address = (mode & 0x40) != 0
                ? actor_address(actor_slot, offset) : kWorkRamBase + offset;
            const std::uint8_t width = mode & 0x3F;
            if (width == 1) {
                const std::uint8_t old = read8(core.ram, address);
                write8(core.ram, address, static_cast<std::uint8_t>(
                    (mode & 0x80) != 0 ? old - value : old + value));
            } else if (width == 2) {
                const std::uint16_t old = read16(core.ram, address);
                write16(core.ram, address, static_cast<std::uint16_t>(
                    (mode & 0x80) != 0 ? old - value : old + value));
            } else if (width == 4) {
                const std::uint32_t old = read32(core.ram, address);
                write32(core.ram, address, (mode & 0x80) != 0
                    ? old - static_cast<std::uint32_t>(value)
                    : old + static_cast<std::uint32_t>(value));
            }
            cursor += 6;
            continue;
        }
        case 0x91:
            movement_callback(core.ram, actor_slot,
                              rom_read32(core.rom, cursor + 2));
            cursor += 6;
            continue;
        case 0x92: {
            const std::uint8_t value = rom_read8(core.rom, cursor + 1);
            if ((value & 0x80) == 0) {
                const std::uint32_t command_pc = cursor;
                actor_write32(actor, kActorMovementReturnPcOffset, command_pc + 6);
                cursor = rom_read32(core.rom, command_pc + 2);
            } else {
                cursor = actor_read32(actor, kActorMovementReturnPcOffset);
            }
            continue;
        }
        case 0x93: {
            const int threshold = rom_read8(core.rom, cursor + 1) == 0xFF
                ? 0x140 : rom_read8(core.rom, cursor + 1);
            const int distance = std::abs(
                static_cast<int>(read16(core.ram, kPlayerWorldX))
                - static_cast<int>(actor_read16(actor, kActorXOffset)));
            cursor = distance <= threshold
                ? rom_read32(core.rom, cursor + 2) : cursor + 6;
            continue;
        }
        case 0x94: {
            const int threshold = rom_read8(core.rom, cursor + 1);
            const int distance = std::abs(
                static_cast<int>(read16(core.ram, kPlayerWorldY))
                - static_cast<int>(actor_read16(actor, kActorYOffset)));
            cursor = distance <= threshold
                ? rom_read32(core.rom, cursor + 2) : cursor + 6;
            continue;
        }
        default:
            result.stopped = true;
            result.completed = true;
            break;
        }
        break;
    }

    actor_write32(actor, kActorMovementPcOffset, cursor);
    result.completed = true;
    result.cursor = cursor;
    return result;
}

void movement_vm_tick_actors(CoreRuntime& core) {
    if (!rom_is_bound(core.rom)) return;
    write8(core.ram, kActorVmMovementPass, 0xFF);
    for (std::size_t slot = 0; slot < kActorSlotCount; ++slot) {
        (void)movement_vm_run_actor(core, slot);
    }
}

}  // namespace openaladdin::core
