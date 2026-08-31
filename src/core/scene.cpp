#include "core/scene.hpp"

#include "core/actor.hpp"
#include "core/level.hpp"
#include "core/ram.hpp"
#include "core/rom.hpp"

namespace openaladdin::core {

void scene_script_complete_to_state1(
    CoreRuntime& core,
    CoreTrace*
) {
    if (read8(core.ram, kSceneScriptPending) != 1) return;

    const RamAddress cursor = read32(core.ram, kSceneScriptData);
    const std::uint8_t value = rom_read8(core.rom, cursor);
    write32(core.ram, kSceneScriptData, cursor + 1);
    if (value != 0) {
        write8(core.ram, kPlayerTerrainQueryResult, value);
        return;
    }

    // The ROM's completion path retires the current actor/resource records
    // before returning through the common scene initialization path.
    for (std::size_t slot = 0; slot < kActorSlotCount; ++slot) {
        actor_clear_and_release(core, slot);
    }
    write8(core.ram, kSceneScriptPending, 0);
    write8(core.ram, kSceneState, 1);
    (void)level_load_from_scene_state(core);
}

}  // namespace openaladdin::core
