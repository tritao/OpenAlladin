#include "movement.hpp"

#include <cstdlib>

namespace openaladdin {

namespace {
constexpr std::uint8_t kActorTerminalType = 0x84;
}

void MovementVm::tick(
    std::array<ActorState, 32>& actors,
    const MovementContext& context
) const {
    if (context.rom.empty()) return;

    const auto read_u8 = [&](std::uint32_t address) -> std::uint8_t {
        if (address >= context.rom.size()) return 0;
        return context.rom[address];
    };
    const auto read_u32 = [&](std::uint32_t address) -> std::uint32_t {
        if (address + 3 >= context.rom.size()) return 0;
        return (static_cast<std::uint32_t>(read_u8(address)) << 24)
            | (static_cast<std::uint32_t>(read_u8(address + 1)) << 16)
            | (static_cast<std::uint32_t>(read_u8(address + 2)) << 8)
            | read_u8(address + 3);
    };
    const auto read_u16 = [&](std::uint32_t address) -> std::uint16_t {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(read_u8(address)) << 8)
            | read_u8(address + 1));
    };
    const auto write_actor_value = [](ActorState& actor, std::uint16_t offset, int size, std::uint32_t value) {
        if (size == 1) {
            const auto byte = static_cast<std::uint8_t>(value);
            switch (offset) {
            case 0x00: actor.type = byte; break;
            case 0x06: actor.movement_flags = byte; break;
            case 0x09: actor.facing_x_flip = byte; break;
            case 0x12: actor.movement_loop_timer = byte; break;
            case 0x35: actor.facing_y_flip = byte; break;
            case 0x36: actor.movement_command_timer = byte; break;
            case 0x3C: actor.flags = byte; break;
            default: break;
            }
        } else if (size == 2) {
            const auto word = static_cast<std::uint16_t>(value);
            switch (offset) {
            case 0x02: actor.x = word; break;
            case 0x04: actor.y = word; break;
            default: break;
            }
        } else {
            switch (offset) {
            case 0x0A: actor.movement_pc = value; break;
            case 0x0E: actor.movement_loop_pc = value; break;
            case 0x14: actor.frame_ptr = value; break;
            case 0x20: actor.animation_pc = value; break;
            case 0x38: actor.movement_return_pc = value; break;
            default: break;
            }
        }
    };

    for (ActorState& actor : actors) {
        // Type 0x34 and the mode-3 sword effect enter MovementVM before
        // their first animation frame is published. Their initial movement
        // command is therefore live even with a null frame pointer.
        if (actor.type == 0 || actor.terminal_timer != 0 || actor.movement_pc == 0
            || (actor.frame_ptr == 0 && actor.type != 0x34 && actor.type != 0x80)) {
            continue;
        }

        const std::uint32_t step_pc = actor.movement_pc;
        std::uint32_t cursor = step_pc;
        if (cursor + 1 >= context.rom.size()) continue;

        // MovementVM_TickActors applies the per-record gravity increment
        // before integrating the vertical accumulator. The increment is
        // enabled by actor +0x06 bit 6; scene-created type-0x2D streams
        // commonly arm it through a 0x91 callback in their first step.
        if ((actor.movement_flags & 0x40) != 0) {
            actor.movement_word_1a = static_cast<std::int16_t>(
                actor.movement_word_1a + 0x78);
        }

        // MovementVM_TickActors integrates the two actor velocity words
        // before consuming the next signed-delta step. The words are 8.8
        // fixed-point values: the ROM-visible pixel coordinate receives the
        // signed high byte, while the full word remains as the accumulator.
        actor.x = static_cast<std::uint16_t>(
            static_cast<int>(actor.x) + (actor.movement_word_18 >> 8));
        actor.y = static_cast<std::uint16_t>(
            static_cast<int>(actor.y) + (actor.movement_word_1a >> 8));
        const auto decay_velocity = [](std::int16_t& velocity, std::int16_t step) {
            if (velocity < 0) {
                if (velocity > static_cast<std::int16_t>(-step)) {
                    velocity = 0;
                } else {
                    velocity = static_cast<std::int16_t>(velocity + step);
                }
            } else if (velocity < step) {
                velocity = 0;
            } else {
                velocity = static_cast<std::int16_t>(velocity - step);
            }
        };
        decay_velocity(actor.movement_word_18, 0x28);
        decay_velocity(actor.movement_word_1a, 0x3C);

        int delta_x = static_cast<std::int8_t>(read_u8(cursor));
        int delta_y = static_cast<std::int8_t>(read_u8(cursor + 1));
        if (actor.facing_x_flip != 0) delta_x = -delta_x;
        if (actor.facing_y_flip != 0) delta_y = -delta_y;
        actor.x = static_cast<std::uint16_t>(static_cast<int>(actor.x) + delta_x);
        actor.y = static_cast<std::uint16_t>(static_cast<int>(actor.y) + delta_y);
        cursor += 2;

        // The ROM applies the signed delta on every tick. The command timer
        // gates only the command dispatch that follows it, so a delayed
        // stream continues moving along the current step each frame.
        if (actor.movement_command_timer != 0) {
            --actor.movement_command_timer;
            continue;
        }

        bool cursor_committed = false;
        for (int command_count = 0; command_count < 32; ++command_count) {
            const std::uint8_t opcode = read_u8(cursor);
            if (opcode < 0x80 || opcode > 0x94) break;

            switch (opcode) {
            case 0x80:  // Movement_Jump: absolute long target.
                cursor = read_u32(cursor + 2);
                if (read_u8(cursor) < 0x80 || read_u8(cursor) > 0x94) {
                    actor.movement_pc = cursor;
                    cursor_committed = true;
                    break;
                }
                continue;
            case 0x81:  // Movement_ToggleFacing.
                if (read_u8(cursor + 1) != 0) {
                    actor.facing_y_flip = static_cast<std::uint8_t>(actor.facing_y_flip ^ 0xFF);
                } else {
                    actor.facing_x_flip = static_cast<std::uint8_t>(actor.facing_x_flip ^ 0xFF);
                }
                cursor += 2;
                continue;
            case 0x82:  // Movement_ClearCursor.
                if (read_u8(cursor + 1) == 0) {
                    actor.movement_pc = 0;
                    cursor_committed = true;
                    break;
                }
                actor.animation_pc = 0;
                cursor += 2;
                continue;
            case 0x83: {  // Movement_WriteActorOrRamValue.
                const std::uint8_t operand = read_u8(cursor + 1);
                const int size = (operand & 0x0F) == 1 || (operand & 0x0F) == 2 ? 2 : 4;
                const std::uint16_t offset = read_u16(cursor + 2);
                const std::uint32_t value = size == 2
                    ? read_u16(cursor + 4)
                    : read_u32(cursor + 4);
                // Absolute work-RAM writes need a RAM model. Actor-relative
                // writes are safe to mirror now and cover the confirmed
                // movement streams that update animation/state cursors.
                if ((operand & 0x10) != 0) {
                    write_actor_value(actor, offset, size, value);
                }
                cursor += static_cast<std::uint32_t>(4 + size);
                continue;
            }
            case 0x84:  // Movement_SetCommandTimer.
                // In movement mode the shared handler uses the high bit to
                // select the per-frame command timer. Without it, the
                // command initializes the separate loop cursor/timer pair.
                if ((read_u8(cursor + 1) & 0x80) != 0) {
                    actor.movement_command_timer = static_cast<std::uint8_t>(read_u8(cursor + 1) & 0x7F);
                    actor.movement_pc = cursor + 2;
                    cursor_committed = true;
                    break;
                } else {
                    actor.movement_loop_timer = read_u8(cursor + 1);
                    actor.movement_loop_pc = cursor + 2;
                }
                // A loop timer is setup inline with the current movement
                // step. The interpreter continues through the rest of that
                // step, so a following arithmetic command is consumed now
                // rather than being mistaken for the next signed delta.
                cursor += 2;
                continue;
            case 0x85:  // Movement_RewindAfterTimer.
                // The original handler consumes one loop count and the
                // interpreter rewinds to the saved loop cursor whenever the
                // counter was non-zero on entry. Re-enter the command loop
                // immediately: the saved cursor points at the inline 0x90
                // command, not at a fresh signed-delta step.
                if (actor.movement_loop_timer != 0) {
                    --actor.movement_loop_timer;
                    cursor = actor.movement_loop_pc;
                    continue;
                }
                cursor += 2;
                continue;
            case 0x87: {  // Movement_AddSignedActorOffset.
                const int delta = static_cast<std::int16_t>(
                    (static_cast<std::uint16_t>(read_u8(cursor + 2)) << 8)
                    | read_u8(cursor + 3));
                if (read_u8(cursor + 1) == 0) {
                    const int mirrored = actor.facing_x_flip != 0 ? -delta : delta;
                    actor.x = static_cast<std::uint16_t>(static_cast<int>(actor.x) + mirrored);
                } else {
                    const int mirrored = actor.facing_y_flip != 0 ? -delta : delta;
                    actor.y = static_cast<std::uint16_t>(static_cast<int>(actor.y) + mirrored);
                }
                cursor += 4;
                continue;
            }
            case 0x88: {  // Movement_PlayerFlagTest.
                // Shared movement command 0x88 maps to animation F2.  Its
                // operand addresses an actor byte and branches on the
                // selected bit (bit 6 selects set vs. clear), retaining the
                // command-stream cursor when the predicate is false.
                const std::uint8_t flags = read_u8(cursor + 1);
                const int bit = flags & 7;
                const std::uint16_t offset = read_u16(cursor + 2);
                const bool set = [&]() {
                    if (offset == 0x09) return (actor.facing_x_flip & (1U << bit)) != 0;
                    if (offset == 0x35) return (actor.facing_y_flip & (1U << bit)) != 0;
                    if (offset == 0x07) return (actor.runtime_field_07 & (1U << bit)) != 0;
                    if (offset == 0x3C) return (actor.flags & (1U << bit)) != 0;
                    return false;
                }();
                const bool branch = (flags & 0x40) != 0 ? set : !set;
                const std::uint32_t target = read_u32(cursor + 4);
                cursor = branch ? target : cursor + 8;
                if (branch
                    && (read_u8(cursor) < 0x80 || read_u8(cursor) > 0x94)) {
                    actor.movement_pc = cursor;
                    cursor_committed = true;
                    break;
                }
                continue;
            }
            case 0x8A: {  // Movement_ActorFieldCompare.
                const std::uint8_t flags = read_u8(cursor + 1);
                const std::uint8_t operation = flags & 0x70;
                const std::uint16_t offset = read_u16(cursor + 2);
                const std::uint16_t value = read_u16(cursor + 4);
                const std::uint32_t current = [&]() -> std::uint32_t {
                    if (offset == 0x09) return actor.facing_x_flip;
                    if (offset == 0x35) return actor.facing_y_flip;
                    if (offset == 0x3C) return actor.flags;
                    if (offset == 0x3D) return actor.interaction_state;
                    return 0;
                }();
                bool branch = false;
                switch (operation) {
                case 0x10: branch = current == value; break;
                case 0x20: branch = current != value; break;
                case 0x30: branch = current >= value; break;
                default: branch = current < value; break;
                }
                const std::uint32_t target = read_u32(cursor + 6);
                cursor = branch ? target : cursor + 10;
                if (branch
                    && (read_u8(cursor) < 0x80 || read_u8(cursor) > 0x94)) {
                    actor.movement_pc = cursor;
                    cursor_committed = true;
                    break;
                }
                continue;
            }
            case 0x90: {  // Movement_AddOrSubtractActorWord.
                const std::uint8_t mode = read_u8(cursor + 1);
                const std::uint16_t offset = read_u16(cursor + 2);
                const auto value = static_cast<std::int16_t>(read_u16(cursor + 4));
                const std::uint8_t width = static_cast<std::uint8_t>(mode & 0x3F);

                // The confirmed sword stream uses actor-relative word
                // arithmetic (mode 0x42, offset +0x1A). Keep the other
                // address/width variants consumed but inert until their RAM
                // targets are identified from a trace.
                if ((mode & 0x40) != 0 && width == 2) {
                    std::int16_t* target = nullptr;
                    if (offset == 0x18) target = &actor.movement_word_18;
                    if (offset == 0x1A) target = &actor.movement_word_1a;
                    if (target != nullptr) {
                        if ((mode & 0x80) != 0) {
                            *target = static_cast<std::int16_t>(*target - value);
                        } else {
                            *target = static_cast<std::int16_t>(*target + value);
                        }
                    }
                }
                cursor += 6;
                continue;
            }
            case 0x8C:  // Movement_ClearActor.
                if (read_u8(cursor + 1) == 0 && (actor.flags & 0x04) == 0) {
                    actor = ActorState{};
                    cursor_committed = true;
                    break;
                }
                cursor += 2;
                continue;
            case 0x8D:  // Movement_FacePlayer.
                actor.facing_x_flip = context.player_world_x < static_cast<int>(actor.x) ? 0xFF : 0;
                cursor += 2;
                continue;
            case 0x8E:  // Movement_SelectDynamicStream.
                // FUN_001AD150 returns the default actor stream in the
                // no-player-state path used by the controlled probe. The
                // remaining global-state selector branches need their own
                // RAM fixtures before they can be mirrored safely.
                actor.movement_pc = 0x00121AD8;
                cursor_committed = true;
                break;
            case 0x91: {  // Movement_PushParameter / tail-call helper.
                // The shared 0xFB handler pushes its long operand over the
                // command-return address, so the helper executes immediately
                // and returns to the movement command loop. Mirror the
                // actor-local helpers observed in the live type-0x2D stream;
                // global callbacks remain consumed but inert until their RAM
                // side effects are modeled.
                const std::uint32_t callback = read_u32(cursor + 2);
                switch (callback) {
                case 0x001ACB5A: actor.flags = static_cast<std::uint8_t>(actor.flags | 0x10); break;
                case 0x001ACB62: actor.flags = static_cast<std::uint8_t>(actor.flags & ~0x10U); break;
                case 0x001ACB6A: actor.movement_flags = static_cast<std::uint8_t>(actor.movement_flags | 0x40); break;
                case 0x001ACB72: actor.movement_flags = static_cast<std::uint8_t>(actor.movement_flags & ~0x40U); break;
                default: break;
                }
                cursor += 6;
                continue;
            }
            case 0x92: {  // Movement_CallOrReturn.
                const std::uint8_t operand = read_u8(cursor + 1);
                if ((operand & 0x80) != 0) {
                    cursor = actor.movement_return_pc;
                } else {
                    actor.movement_return_pc = cursor + 6;
                    cursor = read_u32(cursor + 2);
                }
                if (read_u8(cursor) < 0x80 || read_u8(cursor) > 0x94) {
                    actor.movement_pc = cursor;
                    cursor_committed = true;
                    break;
                }
                continue;
            }
            case 0x93: {  // Movement_PlayerWithinX.
                const int threshold = read_u8(cursor + 1) == 0xFF
                    ? 0x140
                    : read_u8(cursor + 1);
                const int distance = std::abs(context.player_world_x - static_cast<int>(actor.x));
                if (distance <= threshold) {
                    cursor = read_u32(cursor + 2);
                    if (read_u8(cursor) < 0x80 || read_u8(cursor) > 0x94) {
                        actor.movement_pc = cursor;
                        cursor_committed = true;
                        break;
                    }
                    continue;
                }
                // A failed predicate falls through to the alternate command
                // path in the same movement step. The stream itself decides
                // whether that path eventually jumps back to the step root.
                cursor += 6;
                continue;
            }
            case 0x94: {  // Movement_PlayerWithinY.
                const int threshold = read_u8(cursor + 1);
                const int distance = std::abs(context.player_world_y - static_cast<int>(actor.y));
                if (distance <= threshold) {
                    cursor = read_u32(cursor + 2);
                    if (read_u8(cursor) < 0x80 || read_u8(cursor) > 0x94) {
                        actor.movement_pc = cursor;
                        cursor_committed = true;
                        break;
                    }
                    continue;
                }
                // The original Y condition falls through when the player is
                // outside the threshold; the inline jump after the command
                // handles the alternate path.
                cursor += 6;
                continue;
            }
            default:
                // Other conditional, RAM-write, spawn, and velocity commands
                // are intentionally not guessed. Leave the cursor at the command
                // so a trace can identify the next missing handler.
                actor.movement_pc = cursor;
                cursor_committed = true;
                break;
            }
            break;
        }
        if (!cursor_committed) {
            actor.movement_pc = cursor;
        }
    }
}


}  // namespace openaladdin
