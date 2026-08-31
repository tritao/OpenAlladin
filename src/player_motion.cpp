#include "player_motion.hpp"

#include "animation.hpp"
#include "frame_scheduler.hpp"

#include <algorithm>
#include <cstdint>

namespace openaladdin {
namespace {

std::uint8_t fixed_high_byte(std::int16_t value) {
    return static_cast<std::uint8_t>(static_cast<std::uint16_t>(value) >> 8);
}

}  // namespace

PlayerMotionResult PlayerMotionSystem::update_horizontal(
    GameState& state,
    FrameRuntime& runtime,
    const InputState& input,
    const PlayerAnimationVm& animation,
    const PlayerMotionInput& context
) const {
    PlayerState& player = state.player;
    const int direction = (input.right ? 1 : 0) - (input.left ? 1 : 0);
    const bool action_holds_ground_position =
        player.attack_timer != 0
        || animation.stream_kind() == AnimationStreamKind::Action;
    const bool ground_release = context.was_grounded
        && !input.jump_pressed
        && direction == 0
        && runtime.last_ground_direction != 0
        && player.vx == 0
        && player.terrain_response_timer_state == 0
        && !action_holds_ground_position;
    PlayerMotionResult result{
        ground_release,
        ground_release ? runtime.last_ground_direction : 0,
    };

    if (context.was_grounded
        && (player.grounded || context.contour_ground_motion)) {
        if (direction == 0) {
            if (player.terrain_response_timer_state != 0) {
                if (player.animation_selector.state_lock != 0
                    || (animation.stream_kind() == AnimationStreamKind::Action
                        && animation.stream_entry() == 0x001225A2)) {
                    // The A-button selector owns the response timer window
                    // until its state lock expires. The horizontal response
                    // itself is still cleared when the direction is
                    // released; Genesis keeps FFF0CC/response_timer armed
                    // while dropping FFF0B0.
                    player.terrain_horizontal_response = 0;
                    player.animation_selector.horizontal_response = 0;
                    return result;
                }
                if (player.animation_selector.response_state_101 != 0) {
                    // Player_Update's FFF101 branch selects the brake root
                    // while leaving FFF0B0 visible for this boundary. The
                    // following inertial-release pass consumes that value.
                    player.terrain_response_timer_state = 0;
                    player.animation_selector.response_timer = 0;
                    return result;
                }
                // A held ground-response step is not an inertial release.
                // Genesis clears FFF0B0/FFF0CC and returns to locomotion;
                // entering the ordinary brake stream here would execute its
                // unrelated F5 child-spawn sequence.
                player.terrain_horizontal_response = 0;
                player.terrain_response_timer_state = 0;
                player.animation_selector.horizontal_response = 0;
                player.animation_selector.response_timer = 0;
                runtime.last_ground_direction = 0;
            } else if (runtime.last_ground_direction == 0) {
                player.terrain_horizontal_response = 0;
            }
            return result;
        }

        // The recovered ground response uses a three-pixel local step and
        // leaves PLAYER_VX clear. This is separate from airborne 8.8 motion.
        const bool wall_response_stream =
            animation.stream_kind() == AnimationStreamKind::Response
            && animation.stream_entry() == 0x00121FA6;
        if (wall_response_stream) {
            player.terrain_horizontal_response = 0;
            player.terrain_response_timer_state = 0;
            player.vx = 0;
            return result;
        }

        const bool blocked = !context.ignore_horizontal_collision_stop
            && (direction < 0
                ? player.terrain_stop_left_motion != 0
                : player.terrain_stop_right_motion != 0);
        if (!blocked) {
            if (direction < 0) {
                if (player.x >= 0x14) {
                    player.x -= 3;
                }
            } else if (player.x < 0x130) {
                player.x += 3;
            }
        }
        player.terrain_horizontal_response = 3;
        const std::uint8_t response_timer =
            player.terrain_response_timer_state == 0xFF ? 0xFF : 1;
        player.terrain_response_timer_state = response_timer;
        player.animation_selector.horizontal_response = 3;
        player.animation_selector.response_timer = response_timer;
        player.vx = 0;
        return result;
    }

    const auto move_local = [&player](int step) {
        player.x += step;
        player.vx = 0;
    };
    const auto direction_blocked = [&player, direction]() {
        return direction < 0
            ? player.terrain_stop_left_motion != 0
            : player.terrain_stop_right_motion != 0;
    };
    if (player.terrain_response_timer_state != 0
        && input.left && !input.right) {
        move_local(-(player.terrain_horizontal_response != 0
            ? player.terrain_horizontal_response : 2));
    } else if (player.terrain_response_timer_state != 0
               && input.right && !input.left) {
        move_local(player.terrain_horizontal_response != 0
            ? player.terrain_horizontal_response : 2);
    } else if (!context.suppress_active_response_horizontal_motion
               && player.terrain_response_active != 0
               && player.terrain_jump_response_counter != 0
               && input.left && !input.right
               && !direction_blocked()) {
        move_local(-(player.terrain_horizontal_response != 0
            ? player.terrain_horizontal_response : 2));
    } else if (!context.suppress_active_response_horizontal_motion
               && player.terrain_response_active != 0
               && player.terrain_jump_response_counter != 0
               && input.right && !input.left
               && !direction_blocked()) {
        move_local(player.terrain_horizontal_response != 0
            ? player.terrain_horizontal_response : 2);
    } else if (context.terrain_response_was_active
               && player.terrain_response_active == 0
               && player.terrain_response_timer_state == 0
               && input.left && !input.right
               && !direction_blocked()) {
        move_local(-(player.terrain_horizontal_response != 0
            ? player.terrain_horizontal_response : 2));
    } else if (context.terrain_response_was_active
               && player.terrain_response_active == 0
               && player.terrain_response_timer_state == 0
               && input.right && !input.left
               && !direction_blocked()) {
        move_local(player.terrain_horizontal_response != 0
            ? player.terrain_horizontal_response : 2);
    } else if (!context.suppress_active_response_horizontal_motion
               && player.terrain_response_active == 0
               && player.terrain_response_timer_state == 0
               && player.terrain_jump_response_counter != 0
               && input.left && !input.right
               && !direction_blocked()) {
        move_local(-2);
    } else if (!context.suppress_active_response_horizontal_motion
               && player.terrain_response_active == 0
               && player.terrain_response_timer_state == 0
               && player.terrain_jump_response_counter != 0
               && input.right && !input.left
               && !direction_blocked()) {
        move_local(2);
    } else if (player.terrain_response_active != 0
               && player.terrain_jump_response_counter == 0
               && animation.stream_kind() != AnimationStreamKind::Response
               && input.left && !input.right) {
        player.vx = player.vx >= 0
            ? static_cast<std::int16_t>(-0x300)
            : std::max<std::int16_t>(player.vx, -0x300);
    } else if (player.terrain_response_active != 0
               && player.terrain_jump_response_counter == 0
               && animation.stream_kind() != AnimationStreamKind::Response
               && input.right && !input.left) {
        player.vx = player.vx <= 0
            ? static_cast<std::int16_t>(0x300)
            : std::min<std::int16_t>(player.vx, 0x300);
    }
    return result;
}

void PlayerMotionSystem::integrate(GameState& state) const {
    PlayerState& player = state.player;
    // This follows Player_IntegrateMotion at 0x001A9B90: move by the signed
    // high byte of each 8.8 velocity, then damp by 0x28/0x3C.
    if (player.vx != 0) {
        if (player.vx < 0) {
            if (player.terrain_stop_left_motion != 0
                || player.x < 0x14
                || static_cast<std::uint16_t>(-player.vx) < 0x28) {
                player.vx = 0;
            } else {
                player.x += static_cast<std::int8_t>(fixed_high_byte(player.vx));
                player.vx = static_cast<std::int16_t>(player.vx + 0x28);
            }
        } else if (player.vx < 0x28 || player.terrain_stop_right_motion != 0) {
            player.vx = 0;
        } else {
            if (player.x < 0x130) {
                player.x += static_cast<std::int8_t>(fixed_high_byte(player.vx));
            }
            player.vx = static_cast<std::int16_t>(player.vx - 0x28);
        }
    }

    if (player.vy == 0) return;
    if (player.vy < 0) {
        const std::int16_t magnitude = static_cast<std::int16_t>(-player.vy);
        const std::int16_t active_response_stop_threshold =
            player.terrain_response_active != 0
            && player.terrain_jump_response_counter >= 10
            && player.terrain_vertical_stop != 0
            ? 0x0078
            : 0x003B;
        if (player.y < 0x14) {
            player.vy = static_cast<std::int16_t>(player.vy + 0x3C);
        } else if (player.terrain_stop_upward_motion != 0) {
            player.terrain_response_active = 0;
            player.terrain_vertical_stop = 0xFF;
            player.terrain_response_latch = 0;
            player.vy = 0;
        } else if (magnitude > active_response_stop_threshold) {
            player.y += static_cast<std::int8_t>(fixed_high_byte(player.vy));
            player.vy = static_cast<std::int16_t>(player.vy + 0x3C);
        } else {
            player.terrain_vertical_stop = 0xFF;
            player.vy = 0;
        }
    } else if (player.vy > 0x3B) {
        // The fall-phase helper advances VY by 0x78 before this integrator
        // while the result remains below 0x0800. On the crossing frame the
        // helper leaves VY alone, so the integrator uses the old velocity.
        const int advanced_velocity = static_cast<int>(player.vy) + 0x0078;
        const int integration_velocity = advanced_velocity < 0x0800
            ? advanced_velocity
            : player.vy;
        player.y += static_cast<std::int8_t>(fixed_high_byte(
            static_cast<std::int16_t>(integration_velocity)));
        player.vy = static_cast<std::int16_t>(player.vy - 0x3C);
    } else {
        player.terrain_vertical_stop = 0xFF;
        player.vy = 0;
    }
}

void PlayerMotionSystem::finish_ground_release(
    GameState& state,
    FrameRuntime& runtime,
    int direction
) const {
    PlayerState& player = state.player;
    player.terrain_horizontal_response = 0;
    player.animation_selector.horizontal_response = 0;
    player.terrain_response_timer_state = 0;
    player.animation_selector.response_timer = 0;
    player.vx = static_cast<std::int16_t>(direction * 0x038C);
    player.ground_braking = true;
    runtime.last_ground_direction = 0;
}

}  // namespace openaladdin
