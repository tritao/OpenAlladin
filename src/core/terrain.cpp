#include "core/terrain.hpp"

#include <cstddef>

namespace openaladdin::core {
namespace {

constexpr RamAddress kTerrainNoOpHandler = 0x001B65BE;

constexpr std::array<RamAddress, 80> kTerrainHandlers = {{
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
}};

}  // namespace

const std::array<RamAddress, 80>& terrain_handler_table() {
    return kTerrainHandlers;
}

RamAddress terrain_handler_for_behavior(std::uint8_t behavior) {
    return behavior < kTerrainHandlers.size()
        ? kTerrainHandlers[behavior] : kTerrainNoOpHandler;
}

TerrainDispatch terrain_resolve_player_cell(CoreRuntime& core) {
    const std::uint8_t behavior = read8(core.ram, kPlayerTerrainBehavior);
    write8(core.ram, kPlayerTerrainState, 0);
    write8(core.ram, kPlayerTerrainResponseLatch, 0);
    if (behavior != 0x47) {
        write8(core.ram, kPlayerTerrainSurfaceLatch, 0);
    }
    const RamAddress handler = terrain_handler_for_behavior(behavior);
    return TerrainDispatch{
        behavior, handler,
        handler == kTerrainNoOpHandler && behavior != 0x11};
}

}  // namespace openaladdin::core
