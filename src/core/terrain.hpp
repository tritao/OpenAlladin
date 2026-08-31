#pragma once

#include "core/frame.hpp"

#include <array>
#include <cstdint>

namespace openaladdin::core {

struct TerrainDispatch {
    std::uint8_t behavior = 0;
    RamAddress handler = 0;
    bool no_op = true;
};

const std::array<RamAddress, 80>& terrain_handler_table();
RamAddress terrain_handler_for_behavior(std::uint8_t behavior);

// Terrain_ResolvePlayerCell's publication/reset boundary. Resource decoding
// will supply the behavior byte later; for now the already-published RAM
// behavior is dispatched through the recovered ROM table.
TerrainDispatch terrain_resolve_player_cell(CoreRuntime& core);

}  // namespace openaladdin::core
