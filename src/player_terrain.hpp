#pragma once

#include "game_state.hpp"
#include "level.hpp"

#include <cstdint>
#include <optional>

namespace openaladdin {

// Controller values sampled by the recovered player terrain state machine.
// Keeping this small value type independent of Engine avoids making the
// terrain service depend on the SDL-facing input façade.
struct TerrainInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool jump_pressed = false;
};

struct TerrainResponseContext {
    int frame = 0;
    bool scene_transition = false;
    bool preserve_run_response_timer = false;
};

// Owns the player-facing terrain state machine's typed state transitions.
// Animation, actor allocation, and scene side effects are delegated to
// TerrainBehaviorSystem at the recovered scheduler boundary.
class PlayerTerrainSystem {
public:
    void sample(GameState&, const TerrainInput&) const;
    void apply_response(GameState&, const TerrainResponseContext&) const;
    void apply_contour(GameState&, const Level&, bool& terrain_fall_phase) const;

    // Returns the behavior cell whose handler should run at this boundary.
    // The typed player fields are updated even when the returned cell is empty
    // or a no-op, matching Terrain_ResolvePlayerCell's RAM publication.
    std::optional<Level::TerrainCell> resolve(
        GameState&,
        const Level&,
        int previous_world_y,
        std::optional<std::uint8_t> behavior_override = std::nullopt
    ) const;

private:
    static bool has_handler(const Level::TerrainCell&);
    static std::uint32_t handler_for_behavior(std::uint8_t behavior);
};

}  // namespace openaladdin
