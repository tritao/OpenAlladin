#include "core/scene.hpp"

#include "core/actor.hpp"
#include "core/level.hpp"
#include "core/ram.hpp"
#include "core/rom.hpp"
#include "core/trace.hpp"

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

void scene_resource_stream_vdp_record(
    CoreRuntime& core,
    CoreTrace* trace
) {
    if (read8(core.ram, kSceneScriptPending) == 2) return;

    const RamAddress stream = read32(
        core.ram, kSceneResourceVdpStreamPtr);
    if (stream == 0) return;

    std::uint16_t offset = static_cast<std::uint16_t>(
        read16(core.ram, kSceneResourceVdpStreamOffset) + 0x000EU);
    if (offset >= read16(core.ram, kSceneResourceVdpStreamEnd)) offset = 0;
    write16(core.ram, kSceneResourceVdpStreamOffset, offset);

    const RamAddress record = stream + offset;
    const std::uint32_t command_address = rom_read32(core.rom, record);
    write32(core.ram, kVdpCommandAddressLatch, command_address);
    if (trace != nullptr) {
        trace->scene_vdp_record_emitted = true;
        trace->scene_vdp_command_address = command_address;
    }
    for (std::size_t index = 0; index < 5; ++index) {
        const std::uint16_t word = rom_read16(
            core.rom, record + 4 + index * 2);
        if (trace != nullptr) trace->scene_vdp_words[index] = word;
    }
}

void scene_table_select_next_state(
    CoreRuntime& core,
    CoreTrace*
) {
    constexpr RamAddress kSceneTable = 0x00004B04;
    constexpr std::uint16_t kSceneTableEntryCount = 5;
    constexpr std::uint16_t kSceneTableEntrySize = 6;

    std::uint16_t index = read16(core.ram, kSceneTableIndex);
    const std::size_t entry = static_cast<std::size_t>(index)
        * kSceneTableEntrySize;
    write32(core.ram, kSceneScriptData,
            rom_read32(core.rom, kSceneTable + entry));
    write8(core.ram, kSceneState,
           rom_read8(core.rom, kSceneTable + entry + 4));

    index = static_cast<std::uint16_t>(index + 1);
    if (index == kSceneTableEntryCount) index = 0;
    write16(core.ram, kSceneTableIndex, index);
}

}  // namespace openaladdin::core
