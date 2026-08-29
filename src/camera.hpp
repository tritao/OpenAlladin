#pragma once

#include "game_data.hpp"
#include "game_state.hpp"
#include "level.hpp"

namespace openaladdin {

// Owns the recovered Camera_UpdateFollow state transitions. CameraState
// remains in GameState; this class supplies behavior and reads damping data
// through the ROM-backed GameData view.
class CameraSystem {
public:
    void initialize(GameState& state, const Level& level) const;
    bool rebase(GameState& state, const Level& level) const;
    void update(
        GameState& state,
        const Level& level,
        const GameData& data,
        bool suppress_vertical_follow = false
    ) const;
};

}  // namespace openaladdin
