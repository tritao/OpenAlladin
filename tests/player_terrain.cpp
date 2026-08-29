#include "player_terrain.hpp"

#include <cassert>

int main() {
    using namespace openaladdin;

    GameState state;
    PlayerTerrainSystem terrain;
    terrain.sample(state, TerrainInput{true, false, false, true, true});
    assert(state.player.terrain_query_result == 0x56);
    assert(state.player.terrain_push_up == 0xFF);
    assert(state.player.terrain_push_right == 0xFF);
    assert(state.player.terrain_push_down == 0);

    state.player.terrain_query_state_a = 0xFF;
    state.player.terrain_stop_upward_motion = 0;
    const int initial_y = state.player.y;
    terrain.apply_response(state, TerrainResponseContext{0, false, false});
    assert(state.player.y == initial_y - 1);
    assert(state.player.terrain_transition_gate == 0xFF);
    assert(state.camera.vertical_threshold == 400);

    state.player.terrain_query_state_a = 0xFF;
    state.player.terrain_query_state_b = 0xFF;
    state.player.terrain_response_latch = 0xFF;
    state.player.terrain_state = 0xFF;
    Level level;
    assert(!terrain.resolve(state, level, state.camera.y));
    assert(state.player.terrain_query_state_a == 0);
    assert(state.player.terrain_query_state_b == 0);
    assert(state.player.terrain_response_latch == 0);
    assert(state.player.terrain_state == 0);

    return 0;
}
