#include "core/player.hpp"
#include "core/terrain.hpp"

#include <cassert>
#include <cstdint>
#include <string_view>

int main() {
    using namespace openaladdin::core;

    const CoreInput input = core_input_from_token("left+c+b");
    assert(input.left);
    assert(input.jump_held && input.jump_pressed);
    assert(input.attack_held && input.attack_pressed);
    assert(!input.right);

    CoreRuntime core;
    reset(core);
    player_sample_input(core, input);
    assert(read8(core.ram, kTerrainQueryInputRaw) == 0x4B);
    assert(read8(core.ram, kPlayerTerrainQueryResult) == 0x4B);
    assert(read8(core.ram, kPlayerTerrainPushLeft) == 0xFF);
    assert(read8(core.ram, kPlayerTerrainPushRight) == 0);
    assert(read8(core.ram, kPlayerTerrainPushUp) == 0);
    assert(read8(core.ram, kPlayerTerrainPushDown) == 0);

    // Negative horizontal integration consumes the signed high byte and
    // damps the live actor-0 velocity in place.
    write16(core.ram, kPlayerX, 0x0100);
    write_i16(core.ram, kPlayerVelocityX, static_cast<std::int16_t>(-0x0300));
    player_integrate_motion(core);
    assert(read16(core.ram, kPlayerX) == 0x00FD);
    assert(read_i16(core.ram, kPlayerVelocityX) == -0x02D8);

    // Positive vertical integration uses the recovered 0x78 fall helper
    // boundary, then applies the signed high byte and 0x3C damping.
    write16(core.ram, kPlayerY, 0x0100);
    write_i16(core.ram, kPlayerVelocityY, 0x0040);
    player_integrate_motion(core);
    assert(read16(core.ram, kPlayerY) == 0x0100);
    assert(read_i16(core.ram, kPlayerVelocityY) == 0x0004);

    write8(core.ram, kPlayerTerrainBehavior, 0x29);
    write8(core.ram, kPlayerTerrainState, 0xA5);
    write8(core.ram, kPlayerTerrainResponseLatch, 0xA5);
    write8(core.ram, kPlayerTerrainSurfaceLatch, 0xFF);
    const TerrainDispatch launch = terrain_resolve_player_cell(core);
    assert(launch.behavior == 0x29);
    assert(launch.handler == 0x001B557E);
    assert(!launch.no_op);
    assert(read8(core.ram, kPlayerTerrainState) == 0);
    assert(read8(core.ram, kPlayerTerrainResponseLatch) == 0);
    assert(read8(core.ram, kPlayerTerrainSurfaceLatch) == 0);

    write8(core.ram, kPlayerTerrainBehavior, 0x47);
    write8(core.ram, kPlayerTerrainSurfaceLatch, 0xFF);
    const TerrainDispatch surface = terrain_resolve_player_cell(core);
    assert(surface.handler == 0x001B5470);
    assert(!surface.no_op);
    assert(read8(core.ram, kPlayerTerrainSurfaceLatch) == 0xFF);

    write8(core.ram, kPlayerTerrainBehavior, 0x08);
    const TerrainDispatch no_op = terrain_resolve_player_cell(core);
    assert(no_op.handler == 0x001B65BE);
    assert(no_op.no_op);
    assert(terrain_handler_for_behavior(0xFF) == 0x001B65BE);

    // The new frame overload consumes input as an argument and publishes the
    // query before the recovered motion/integrator and later coordinate pass.
    write_i16(core.ram, kPlayerVelocityX, 0);
    write_i16(core.ram, kPlayerVelocityY, 0);
    step_frame(core, 1, CoreInput{}, "none");
    assert(read8(core.ram, kFramePhaseCounter) == 1);
    step_frame(core, 2, CoreInput{false, false, false, true}, "right");
    assert(read8(core.ram, kPlayerTerrainPushRight) == 0xFF);
    assert(read16(core.ram, kPlayerWorldX)
        == static_cast<std::uint16_t>(read16(core.ram, kWorldCameraX)
            + read16(core.ram, kPlayerX)));

    return 0;
}
