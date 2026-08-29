#include "player_terrain.hpp"

namespace openaladdin {
namespace {

constexpr std::uint32_t kTerrainNoOpHandler = 0x001B65BE;

// TerrainHandlerTable is ROM-owned dispatch metadata. Keeping it beside the
// state machine makes the Engine independent of terrain handler addresses.
constexpr std::uint32_t kTerrainHandlers[] = {
    0x001B65BE, 0x001B5492, 0x001B5492, 0x001B5492,
    0x001B5492, 0x001B549C, 0x001B549C, 0x001B549C,
    0x001B65BE, 0x001B54F4, 0x001B5320, 0x001B54F4,
    0x001B54F4, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B5450, 0x001B65BE, 0x001B56F4, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B5458, 0x001B5460,
    0x001B5468, 0x001B65BE, 0x001B65BE, 0x001B575C,
    0x001B5764, 0x001B576C, 0x001B65BE, 0x001B5774,
    0x001B5318, 0x001B65BE, 0x001B54D8, 0x001B54D8,
    0x001B54D2, 0x001B54E0, 0x001B65BE, 0x001B54A6,
    0x001B55E8, 0x001B557E, 0x001B55D8, 0x001B5502,
    0x001B65BE, 0x001B56B6, 0x001B65BE, 0x001B65BE,
    0x001B537A, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B536C, 0x001B53A2, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B5470,
};

}  // namespace

std::uint32_t PlayerTerrainSystem::handler_for_behavior(std::uint8_t behavior) {
    return behavior < std::size(kTerrainHandlers)
        ? kTerrainHandlers[behavior]
        : kTerrainNoOpHandler;
}

bool PlayerTerrainSystem::has_handler(const Level::TerrainCell& cell) {
    return cell.valid && cell.behavior != 0
        && (cell.handler != kTerrainNoOpHandler || cell.behavior == 0x11);
}

void PlayerTerrainSystem::sample(GameState& state, const TerrainInput& input) const {
    auto& player = state.player;
    player.terrain_query_result = 0x7F;
    if (input.up) player.terrain_query_result &= static_cast<std::uint8_t>(~0x01);
    if (input.down) player.terrain_query_result &= static_cast<std::uint8_t>(~0x02);
    if (input.left) player.terrain_query_result &= static_cast<std::uint8_t>(~0x04);
    if (input.right) player.terrain_query_result &= static_cast<std::uint8_t>(~0x08);
    if (input.jump_pressed) player.terrain_query_result &= static_cast<std::uint8_t>(~0x20);

    player.terrain_push_right = input.right ? 0xFF : 0;
    player.terrain_push_left = input.left ? 0xFF : 0;
    player.terrain_push_up = input.up ? 0xFF : 0;
    player.terrain_push_down = input.down ? 0xFF : 0;
}

void PlayerTerrainSystem::apply_response(
    GameState& state,
    const TerrainResponseContext& context
) const {
    auto& player = state.player;
    auto& camera = state.camera;
    // FUN_001A986E consumes the query latch published by the terrain handler.
    if (context.scene_transition || player.terrain_query_state_a == 0) {
        player.terrain_transition_gate = 0;
        return;
    }
    if (player.terrain_terminal_transition != 0) return;
    if (player.terrain_response_active == 0
        && player.terrain_response_timer_state != 0
        && context.preserve_run_response_timer) {
        player.terrain_transition_gate = 0;
        return;
    }

    if (player.vy <= 0) {
        if (player.terrain_response_active == 0) {
            if (player.terrain_push_up != 0) {
                if (player.terrain_stop_upward_motion == 0
                    && player.terrain_query_state_b == 0) {
                    const int step = (context.frame & 1U) != 0 ? 2 : 1;
                    player.y -= step;
                    camera.vertical_threshold = 400;
                    camera.update_delay = 0;
                }
                player.terrain_transition_gate = 0xFF;
                return;
            }
            if (player.terrain_push_down != 0) {
                if (player.terrain_landing_state != 0) {
                    player.terrain_transition_gate = 0;
                    return;
                }
                player.y += 2;
                camera.vertical_threshold = 0x150;
                camera.update_delay = 0;
                player.terrain_transition_gate = 0xFF;
                return;
            }
        } else if (player.terrain_vertical_stop == 0) {
            player.terrain_transition_gate = 0;
            return;
        }
    }

    if (player.terrain_stop_upward_motion == 0) {
        player.vx = 0;
        player.vy = 0;
        player.terrain_horizontal_response = 0;
        player.terrain_response_active = 0;
        player.terrain_jump_response_counter = 0;
        player.terrain_response_timer_state = 0;
        player.terrain_transition_gate = 0xFF;
        return;
    }
    player.terrain_transition_gate = 0;
}

void PlayerTerrainSystem::apply_contour(
    GameState& state,
    const Level& level,
    bool& terrain_fall_phase
) const {
    auto& player = state.player;
    auto& camera = state.camera;
    const Level::TerrainContour contour = level.query_player_contour(
        camera.x + player.x, camera.y + player.y, player.terrain_surface_mode);
    if (!contour.valid) {
        player.terrain_landing_state = 0;
        player.grounded = false;
        return;
    }

    if ((player.terrain_response_active != 0 && player.terrain_vertical_stop == 0)
        || (!player.grounded && player.vy < 0 && player.terrain_vertical_stop == 0)) {
        if (player.terrain_landing_state == 0xFF) {
            player.grounded = false;
            return;
        }
        player.terrain_landing_state = 0;
        player.grounded = false;
        return;
    }

    const int target_y = contour.target_world_y - camera.y;
    const int delta = target_y - player.y;
    if (delta < -8 || delta > 8) {
        player.terrain_landing_state = 0;
        player.grounded = false;
        return;
    }

    player.y = target_y;
    player.vy = 0;
    player.grounded = contour.contour == 1;
    player.terrain_landing_state = contour.contour;
    player.terrain_response_active = 0;
    player.terrain_jump_response_counter = 0;
    player.terrain_response_timer_state = 0;
    terrain_fall_phase = false;
}

std::optional<Level::TerrainCell> PlayerTerrainSystem::resolve(
    GameState& state,
    const Level& level,
    int previous_world_y,
    std::optional<std::uint8_t> behavior_override
) const {
    auto& player = state.player;
    const int world_x = state.camera.x + player.x;
    const int world_y = state.camera.y + player.y;
    Level::TerrainQuery query = level.query_player(world_x, world_y);
    if (behavior_override && query.resolver.valid) {
        query.resolver.behavior = *behavior_override;
        query.resolver.handler = handler_for_behavior(*behavior_override);
    }
    const Level::TerrainCell previous_down_probe =
        level.resolve_player_cell(world_x, previous_world_y + 8);
    const Level::TerrainCell* cell = &query.resolver;
    if (has_handler(query.resolver) == false
        && player.vy >= 0
        && has_handler(previous_down_probe)) {
        cell = &previous_down_probe;
    }
    player.terrain_behavior = cell->valid ? cell->behavior : 0;
    player.terrain_query_state_a = 0;
    player.terrain_query_state_b = 0;
    player.terrain_response_latch = 0;
    player.terrain_state = 0;
    if (player.terrain_behavior != 0x47) {
        player.terrain_surface_latch = 0;
    }

    if (!has_handler(*cell)) return std::nullopt;
    return *cell;
}

}  // namespace openaladdin
