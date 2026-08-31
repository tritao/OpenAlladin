#include "core/frame.hpp"
#include "core/trace.hpp"

#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>

int main() {
    using namespace openaladdin::core;

    static_assert(kWorkRamSize == 0x10000);
    static_assert(kActorTableEnd == 0x00FF867F);
    static_assert(actor_address(0, 0) == kActorTableBase);
    static_assert(actor_address(31, 0) == 0x00FF863E);

    CoreRuntime core;
    reset(core);
    const std::uint8_t rom_bytes[] = {0x12, 0x34, 0x56, 0x78};
    bind_rom(core, RomView{rom_bytes, sizeof(rom_bytes)});
    assert(rom_is_bound(core.rom));
    assert(rom_read8(core.rom, 0) == 0x12);
    assert(rom_read16(core.rom, 1) == 0x3456);
    assert(rom_read32(core.rom, 0) == 0x12345678);
    assert(rom_read8(core.rom, sizeof(rom_bytes)) == 0);
    for (const std::uint8_t byte : core.ram.bytes) assert(byte == 0);

    // Genesis is big-endian. The helpers are deliberately the only way this
    // test touches multi-byte RAM state.
    write8(core.ram, kPlayerX, 0x12);
    write8(core.ram, kPlayerX + 1, 0x34);
    assert(read16(core.ram, kPlayerX) == 0x1234);
    write16(core.ram, kPlayerY, 0x5678);
    assert(read8(core.ram, kPlayerY) == 0x56);
    assert(read8(core.ram, kPlayerY + 1) == 0x78);
    write32(core.ram, kPlayerAnimationPc, 0x001223DA);
    assert(read32(core.ram, kPlayerAnimationPc) == 0x001223DA);
    assert(read_i16(core.ram, kPlayerY) == 0x5678);
    write_i16(core.ram, kPlayerVelocityY, static_cast<std::int16_t>(-0x200));
    assert(read_i16(core.ram, kPlayerVelocityY) == -0x200);

    // The actor record is a direct RAM view. A write through slot 7 is visible
    // at its absolute address without an intermediate actor array.
    const ActorView actor7 = actor_view(core.ram, 7);
    actor_write8(actor7, 0x00, 0x42);
    actor_write16(actor7, 0x02, 0xABCD);
    actor_write32(actor7, 0x20, 0x00123456);
    assert(read8(core.ram, actor_address(7, 0x00)) == 0x42);
    assert(read16(core.ram, actor_address(7, 0x02)) == 0xABCD);
    assert(read32(core.ram, actor_address(7, 0x20)) == 0x00123456);

    // Published world coordinates are stored fields, not computed aliases.
    write16(core.ram, kWorldCameraX, 0x1000);
    write16(core.ram, kWorldCameraY, 0x0200);
    write16(core.ram, kPlayerX, 0x0123);
    write16(core.ram, kPlayerY, 0x0045);
    write16(core.ram, kPlayerWorldX, 0xEEEE);
    write16(core.ram, kPlayerWorldY, 0xDDDD);
    assert(player_world_x(core.ram) == 0xEEEE);
    assert(player_world_y(core.ram) == 0xDDDD);
    player_publish_world_coordinates(core);
    assert(player_world_x(core.ram) == 0x1123);
    assert(player_world_y(core.ram) == 0x0245);

    // FFF101 is ordinary RAM, not a member of a typed player object.
    write8(core.ram, kPlayerTerrainBrakeState, 0xA5);
    assert(core.ram.bytes[work_ram_offset(kPlayerTerrainBrakeState)] == 0xA5);

    std::ostringstream trace_output;
    CoreTrace trace;
    trace_begin(trace, trace_output, &core);
    trace_state(trace, core, 0, "none", false);
    step_frame(core, 1, "right", &trace);
    assert(read8(core.ram, kFramePhaseCounter) == 1);
    assert(trace.phase_count == kFrameServiceCount + 1);
    assert(trace.write_count == 8);
    assert(trace.phases[0].name == std::string("Game_FrameUpdateLoop"));
    assert(trace.phases[2].name == std::string("Player_PublishWorldCoordinates"));
    assert(trace.writes[0].address == kPlayerWorldX);
    assert(trace.writes[0].value == 0x1123);
    assert(trace.writes[7].address == kPlayerWorldY);

    const std::string output = trace_output.str();
    assert(output.find("\"format\":\"openaladdin-core-trace-v1\"") != std::string::npos);
    assert(output.find("\"rom_size\":4") != std::string::npos);
    assert(output.find("\"ram_bytes\":[") != std::string::npos);
    assert(output.find("\"address\":16743930") != std::string::npos);
    assert(output.find("\"actors\":[") != std::string::npos);
    assert(output.find("\"slot\":31") != std::string::npos);
    assert(output.find("\"phase_order\":[") != std::string::npos);
    assert(output.find("\"publication_writes\":[") != std::string::npos);

    return 0;
}
