#include "core/player.hpp"

#include <cstdint>

namespace openaladdin::core {
namespace {

int signed_high_byte(std::int16_t value) {
    return static_cast<std::int8_t>(
        static_cast<std::uint16_t>(value) >> 8);
}

bool token_part(std::string_view token, std::size_t start, std::size_t end,
                std::string_view expected) {
    return token.substr(start, end - start) == expected;
}

constexpr std::uint32_t kPlayerIdleStream = 0x00121D9A;
constexpr std::uint32_t kPlayerRunStream = 0x00122006;
constexpr std::uint32_t kPlayerJumpStream = 0x001221B0;
constexpr std::uint32_t kPlayerTimedJumpStream = 0x001220B8;
constexpr std::uint32_t kPlayerAppleStream = 0x001223DA;
constexpr std::uint32_t kPlayerSwordStream = 0x0012271A;
constexpr std::uint32_t kPlayerActionSpecialCameraStream = 0x001226E4;
constexpr std::uint32_t kPlayerActionTransitionStream = 0x00122AF6;
constexpr std::uint32_t kPlayerActionTerrainTimerStream = 0x001228AC;
constexpr std::uint32_t kPlayerActionAirborneStream = 0x0012295E;
constexpr std::uint32_t kPlayerActionPushDownStream = 0x001227D2;
constexpr std::uint32_t kPlayerActionPushUpStream = 0x00122A10;

bool in_range(std::uint32_t value, std::uint32_t first, std::uint32_t last) {
    return value >= first && value <= last;
}

bool is_action_cursor(std::uint32_t cursor) {
    return in_range(cursor, 0x001223DA, 0x00122B57);
}

bool is_idle_cursor(std::uint32_t cursor) {
    return in_range(cursor, kPlayerIdleStream, 0x00121F83);
}

bool is_run_cursor(std::uint32_t cursor) {
    return in_range(cursor, kPlayerRunStream, 0x001220A6);
}

void select_stream(CoreRuntime& core, std::uint32_t stream) {
    write32(core.ram, kPlayerAnimationPc, stream);
    write8(core.ram, kPlayerAnimationTimer, 0);
}

}  // namespace

CoreInput core_input_from_token(std::string_view token) {
    CoreInput input;
    std::size_t start = 0;
    while (start <= token.size()) {
        const std::size_t end = token.find('+', start);
        const std::size_t part_end = end == std::string_view::npos
            ? token.size() : end;
        if (token_part(token, start, part_end, "up")) {
            input.up = true;
        } else if (token_part(token, start, part_end, "down")) {
            input.down = true;
        } else if (token_part(token, start, part_end, "left")) {
            input.left = true;
        } else if (token_part(token, start, part_end, "right")) {
            input.right = true;
        } else if (token_part(token, start, part_end, "a")
                   || token_part(token, start, part_end, "apple")
                   || token_part(token, start, part_end, "throw")) {
            input.apple_held = true;
            input.apple_pressed = true;
        } else if (token_part(token, start, part_end, "b")
                   || token_part(token, start, part_end, "attack")
                   || token_part(token, start, part_end, "sword")) {
            input.attack_held = true;
            input.attack_pressed = true;
        } else if (token_part(token, start, part_end, "c")
                   || token_part(token, start, part_end, "jump")
                   || token_part(token, start, part_end, "space")) {
            input.jump_held = true;
            input.jump_pressed = true;
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return input;
}

void player_sample_input(CoreRuntime& core, const CoreInput& input) {
    // This is the active-low query byte published by the recovered input
    // service. The raw controller sample is kept separate from the derived
    // terrain query result; neither is a C++ mirror of the player.
    std::uint8_t query = 0x7F;
    if (input.up) query &= static_cast<std::uint8_t>(~0x01U);
    if (input.down) query &= static_cast<std::uint8_t>(~0x02U);
    if (input.left) query &= static_cast<std::uint8_t>(~0x04U);
    if (input.right) query &= static_cast<std::uint8_t>(~0x08U);
    if (input.attack_held) query &= static_cast<std::uint8_t>(~0x10U);
    if (input.jump_held || input.jump_pressed) {
        query &= static_cast<std::uint8_t>(~0x20U);
    }

    write8(core.ram, kTerrainQueryInputRaw, query);
    write8(core.ram, kPlayerTerrainQueryResult, query);
    write8(core.ram, kPlayerTerrainPushRight, input.right ? 0xFF : 0);
    write8(core.ram, kPlayerTerrainPushLeft, input.left ? 0xFF : 0);
    write8(core.ram, kPlayerTerrainPushUp, input.up ? 0xFF : 0);
    write8(core.ram, kPlayerTerrainPushDown, input.down ? 0xFF : 0);
}

void player_select_locomotion_or_action(
    CoreRuntime& core,
    const CoreInput& input
) {
    if (actor_read8(actor_view(core.ram, 0), kActorTypeOffset) == 0) return;
    const std::uint32_t cursor = read32(core.ram, kPlayerAnimationPc);
    if (is_action_cursor(cursor)
        || read8(core.ram, kPlayerTerrainResponseActive) != 0
        || read_i16(core.ram, kPlayerVelocityY) != 0) {
        return;
    }

    const bool moving = input.left != input.right;
    if (moving && !is_run_cursor(cursor)) {
        select_stream(core, kPlayerRunStream);
    } else if (!moving && !is_idle_cursor(cursor)) {
        select_stream(core, kPlayerIdleStream);
    }
}

void player_select_action_animation(
    CoreRuntime& core,
    const CoreInput& input
) {
    if (!input.attack_pressed && !input.apple_pressed) return;
    if (actor_read8(actor_view(core.ram, 0), kActorTypeOffset) == 0) return;
    if (read32(core.ram, kPlayerFramePointer) == 0
        || read8(core.ram, kPlayerActionAnimationState) != 0
        || read8(core.ram, kPlayerInteractionAnimationGate) != 0
        || read8(core.ram, kPlayerTerminalTransition) != 0
        || read8(core.ram, kPlayerInteractionLock) != 0) {
        return;
    }

    // A-button action selection has its own stream family. The exact
    // inventory gate and child spawn are VM contracts; this boundary only
    // publishes the recovered root when the input is accepted.
    if (input.apple_pressed) {
        select_stream(core, kPlayerAppleStream);
        return;
    }

    const bool grounded = read8(core.ram, kPlayerTerrainLandingState) != 0
        && read_i16(core.ram, kPlayerVelocityY) == 0;
    std::uint32_t stream = kPlayerActionAirborneStream;
    if (read8(core.ram, kCameraSpecialMode) != 0) {
        stream = kPlayerActionSpecialCameraStream;
    } else if (read8(core.ram, kPlayerTransitionLock) != 0
               || read8(core.ram, kPlayerTransitionGate) != 0) {
        stream = kPlayerActionTransitionStream;
    } else if (read8(core.ram, kPlayerTerrainResponseTimer) != 0) {
        stream = grounded
            ? kPlayerActionTerrainTimerStream
            : kPlayerActionAirborneStream;
    } else if (grounded) {
        if (read8(core.ram, kPlayerTerrainPushDownState) != 0) {
            stream = kPlayerActionPushDownStream;
        } else if (read8(core.ram, kPlayerTerrainPushUpState) != 0) {
            stream = kPlayerActionPushUpStream;
        } else {
            stream = kPlayerSwordStream;
        }
    }
    select_stream(core, stream);
    write8(core.ram, kPlayerInteractionPending, 10);
}

void player_handle_jump_and_vertical_state(
    CoreRuntime& core,
    const CoreInput& input
) {
    GenesisRam& ram = core.ram;
    const bool active_response = read8(ram, kPlayerTerrainResponseActive) != 0;
    const std::uint8_t response_counter =
        read8(ram, kPlayerTerrainJumpResponseCounter);
    if (active_response && response_counter != 0 && response_counter < 10) {
        write8(ram, kPlayerTerrainJumpResponseCounter,
               static_cast<std::uint8_t>(response_counter + 1));
        if (input.jump_held && read8(ram, kPlayerActionAnimationState) == 0) {
            write_i16(ram, kPlayerVelocityY, static_cast<std::int16_t>(
                read_i16(ram, kPlayerVelocityY) - 0x006C));
        }
    }

    if (!input.jump_pressed
        || read8(ram, kPlayerTerrainLandingState) == 0
        || read_i16(ram, kPlayerVelocityY) != 0
        || active_response) {
        return;
    }

    write_i16(ram, kPlayerVelocityY, static_cast<std::int16_t>(-0x0200));
    write8(ram, kPlayerTerrainResponseActive, 0xFF);
    write8(ram, kPlayerTerrainJumpResponseCounter, 1);
    if (!input.left && !input.right) {
        write8(ram, kPlayerTerrainResponseTimer, 0);
    }
    write8(ram, kPlayerTerrainVerticalStop, 0);
    write16(ram, kCameraHorizontalThreshold, 0x00B0);
    write16(ram, kCameraVerticalThreshold, 0x0170);
    write8(ram, kCameraUpdateDelay, 7);
    select_stream(
        core,
        read8(ram, kPlayerTerrainResponseTimer) != 0
            ? kPlayerTimedJumpStream : kPlayerJumpStream);
}

void player_integrate_motion(CoreRuntime& core) {
    GenesisRam& ram = core.ram;
    std::int16_t velocity_x = read_i16(ram, kPlayerVelocityX);
    const int local_x = static_cast<int>(read_i16(ram, kPlayerX));

    if (velocity_x < 0) {
        const int magnitude = -static_cast<int>(velocity_x);
        if (read8(ram, kTerrainStopLeftMotion) != 0
            || local_x < 0x14
            || magnitude < 0x28) {
            velocity_x = 0;
        } else {
            write16(ram, kPlayerX, static_cast<std::uint16_t>(
                local_x + signed_high_byte(velocity_x)));
            velocity_x = static_cast<std::int16_t>(velocity_x + 0x28);
        }
    } else if (velocity_x > 0) {
        if (velocity_x < 0x28
            || read8(ram, kTerrainStopRightMotion) != 0) {
            velocity_x = 0;
        } else {
            if (local_x < 0x130) {
                write16(ram, kPlayerX, static_cast<std::uint16_t>(
                    local_x + signed_high_byte(velocity_x)));
            }
            velocity_x = static_cast<std::int16_t>(velocity_x - 0x28);
        }
    }
    write_i16(ram, kPlayerVelocityX, velocity_x);

    std::int16_t velocity_y = read_i16(ram, kPlayerVelocityY);
    if (velocity_y == 0) return;

    if (velocity_y < 0) {
        const int magnitude = -static_cast<int>(velocity_y);
        const int active_response_stop_threshold =
            read8(ram, kPlayerTerrainResponseActive) != 0
                && read8(ram, kPlayerTerrainJumpResponseCounter) >= 10
                && read8(ram, kPlayerTerrainVerticalStop) != 0
            ? 0x78 : 0x3B;
        const int local_y = static_cast<int>(read_i16(ram, kPlayerY));
        if (local_y < 0x14) {
            velocity_y = static_cast<std::int16_t>(velocity_y + 0x3C);
        } else if (read8(ram, kTerrainStopUpwardMotion) != 0) {
            write8(ram, kPlayerTerrainResponseActive, 0);
            write8(ram, kPlayerTerrainVerticalStop, 0xFF);
            write8(ram, kPlayerTerrainResponseLatch, 0);
            velocity_y = 0;
        } else if (magnitude > active_response_stop_threshold) {
            write16(ram, kPlayerY, static_cast<std::uint16_t>(
                local_y + signed_high_byte(velocity_y)));
            velocity_y = static_cast<std::int16_t>(velocity_y + 0x3C);
        } else {
            write8(ram, kPlayerTerrainVerticalStop, 0xFF);
            velocity_y = 0;
        }
    } else if (velocity_y > 0x3B) {
        // The fall helper may advance VY by 0x78 before this routine. On the
        // 0x0800 crossing it leaves the old velocity for this integration.
        const int advanced_velocity = static_cast<int>(velocity_y) + 0x78;
        const int integration_velocity = advanced_velocity < 0x0800
            ? advanced_velocity : velocity_y;
        const int local_y = static_cast<int>(read_i16(ram, kPlayerY));
        write16(ram, kPlayerY, static_cast<std::uint16_t>(
            local_y + signed_high_byte(static_cast<std::int16_t>(
                integration_velocity))));
        velocity_y = static_cast<std::int16_t>(velocity_y - 0x3C);
    } else {
        write8(ram, kPlayerTerrainVerticalStop, 0xFF);
        velocity_y = 0;
    }
    write_i16(ram, kPlayerVelocityY, velocity_y);
}

}  // namespace openaladdin::core
