#include "terrain_behavior.hpp"

#include "actor_lifecycle.hpp"
#include "animation_system.hpp"
#include "frame_scheduler.hpp"
#include "game_state.hpp"
#include "interaction.hpp"

namespace openaladdin {
namespace {

constexpr std::uint32_t kTerrainScene5SpawnTemplate = 0x001B805C;
constexpr std::uint32_t kTerrainScene5SpawnAnimationDefault = 0x001250BA;
constexpr std::uint32_t kTerrainScene5SpawnAnimationLow = 0x001250CE;
constexpr std::uint32_t kTerrainScene5SpawnAnimationHigh = 0x001250DE;
constexpr int kTerrainResourceBase = 0x2FD2;

}  // namespace

void TerrainBehaviorSystem::apply(
    GameState& state,
    const Level::TerrainCell& cell,
    FrameRuntime& runtime,
    std::span<const std::uint8_t> rom
) const {
    auto& player = state.player;
    auto& camera = state.camera;
    const int world_x = camera.x + player.x;
    const int world_y = camera.y + player.y;

    switch (cell.behavior) {
    case 0x01:  // TerrainHandler_ClearSurfaceModeBlock (0x001B5492)
    case 0x02:
    case 0x03:
    case 0x04:
        // The four low surface behaviors share the ROM clear block.
        player.terrain_surface_mode = 0;
        break;
    case 0x05:  // TerrainHandler_SetSurfaceModeBlock (0x001B549C)
    case 0x06:
    case 0x07:
        // The three upper surface behaviors share the ROM set block.
        player.terrain_surface_mode = 1;
        break;
    case 0x0A:  // TerrainHandler_SurfaceInteraction (0x001B5320)
        services_.interactions.apply_surface_terrain_behavior(state);
        break;
    case 0x20:  // TerrainHandler_SetTerminalCollision (0x001B5318)
        player.terrain_terminal_transition = 0xFF;
        break;
    case 0x22:  // TerrainHandler_SetQueryStateA (0x001B54D8)
    case 0x23:
        player.terrain_query_state_a = 0xFF;
        break;
    case 0x24:  // TerrainHandler_SetQueryStateAB (0x001B54D2)
        // The ROM writes FFF0CF and falls through to 0x001B54D8, which writes
        // FFF0CE before returning.
        player.terrain_query_state_b = 0xFF;
        player.terrain_query_state_a = 0xFF;
        break;
    case 0x25: {  // TerrainHandler_SetTerrainStateBlock (0x001B54E0)
        player.terrain_state = 0xFF;
        if (state.scene.state != 5
            || (player.terrain_push_left == 0 && player.terrain_push_right == 0)) {
            break;
        }

        // FUN_001B3032: the scene-5 branch advances the shared 32-bit state
        // with state = state * 13 + 7, then uses the low byte as D7. The ROM
        // only allocates when that byte is below 0x28.
        state.random.value = state.random.value * 13U + 7U;
        const std::uint8_t random_value = static_cast<std::uint8_t>(
            state.random.value ^ (state.random.value >> 16)
        );
        if (random_value >= 0x28) {
            break;
        }

        // Actor_FindFreeSlot (0x001AE262) scans the common records at slots
        // 3..22. Actor_InitializeFromTemplate copies the fixed template at
        // 0x001B805C into the selected record.
        const auto free_slot = services_.actors.allocate(ActorPool::CommonForward);
        if (!free_slot) break;

        const auto read_rom_u8 = [rom](std::uint32_t address) -> std::uint8_t {
            return address < rom.size() ? rom[address] : 0;
        };
        ActorState spawned = services_.actors.initialize_record(
            state.actors[*free_slot], kTerrainScene5SpawnTemplate);
        // The template source byte is clear, but the terrain response's
        // runtime initializer enables actor-motion bit 6 before the record is
        // next observed in RAM (confirmed at +0x06 in the MAME capture).
        spawned.movement_flags = static_cast<std::uint8_t>(
            read_rom_u8(kTerrainScene5SpawnTemplate + 0x06) | 0x40);
        spawned.x = static_cast<std::uint16_t>(
            runtime.terrain_input_world_x + static_cast<int>(random_value & 7U) - 3);
        spawned.y = static_cast<std::uint16_t>(runtime.terrain_input_world_y - 0x2A);
        if (random_value < 0x1B) {
            spawned.animation_pc = random_value < 0x0D
                ? kTerrainScene5SpawnAnimationLow
                : kTerrainScene5SpawnAnimationHigh;
        } else {
            spawned.animation_pc = kTerrainScene5SpawnAnimationDefault;
        }
        if (!services_.actors.install(*free_slot, spawned)) break;
        services_.animation.actors().vm(*free_slot).clear_actor_service_boundary();
        services_.animation.actors().vm(*free_slot).defer_actor_service();
        break;
    }
    case 0x27:  // TerrainHandler_TransitionResponse (0x001B54A6)
        // Exact ROM body: set FFF0CF, SUBI.W #$50,PLAYER_Y, select the
        // transition stream, clear its timer, set FFF0E7, and clear FFF0CC.
        player.terrain_query_state_b = 0xFF;
        player.y -= 0x50;
        services_.animation.player().select_response_stream(0x001223D0);
        player.terrain_response_active = 0xFF;
        player.terrain_response_timer_state = 0;
        break;
    case 0x28:  // TerrainHandler_StopAndAlign (0x001B55E8)
        // The ROM ignores the response while the animation gate or another
        // terrain response is active. The accepted branch clears velocity
        // and response state, selects the stop stream, snaps to the aligned
        // world tile, and arms the four-frame transition countdown.
        if (player.terrain_response_active != 0) {
            break;
        }
        player.vx = 0;
        player.vy = 0;
        player.terrain_horizontal_response = 0;
        player.terrain_response_timer_state = 0;
        player.terrain_transition_countdown = 4;
        player.terrain_response_active = 0;
        player.grounded = true;
        player.x = ((world_x | 0x1F) - camera.x) - 8;
        player.y = ((world_y & ~0x0F) - camera.y) + 4;
        services_.animation.player().select_response_stream(0x00121964);
        break;
    case 0x29:  // TerrainHandler_LaunchPlayerBlock (0x001B557E)
        // Launch is accepted only when the previous terrain response has
        // completed. The ROM writes the launch velocities, clears the
        // vertical-stop/timer latches, and marks the response active.
        if (player.terrain_response_active != 0) {
            break;
        }
        player.vx = static_cast<std::int16_t>(-0x400);
        player.vy = static_cast<std::int16_t>(-0x500);
        player.terrain_vertical_stop = 0;
        player.terrain_response_timer_state = 0;
        player.terrain_response_active = 0xFF;
        break;
    case 0x2A:  // TerrainHandler_DiagonalCorrection (0x001B55D8)
        // Exact ROM body: ADDQ.W #1,PLAYER_X; ADDI.W #-0x46,PLAYER_VX.
        // The normal integrator consumes the resulting high-byte displacement
        // later in the same frame, so the visible net X change can be zero.
        player.x += 1;
        player.vx = static_cast<std::int16_t>(player.vx - 0x46);
        break;
    case 0x2B: {  // TerrainHandler_StopAndAlignPlayer (0x001B5502)
        // The ROM ignores this response while the animation gate is set, VY
        // is negative, or the landing state is already active. The fixture
        // exposes the accepted branch; the normal path has the same guards
        // represented by these native state fields.
        if (player.vy < 0 || player.terrain_landing_state != 0) {
            break;
        }
        player.vx = 0;
        player.terrain_response_active = 0;
        player.terrain_response_timer_state = 0;
        player.terrain_horizontal_response = 0;
        player.terrain_landing_state = 0;
        player.x = ((world_x & ~0x0F) - camera.x) + 6;
        services_.animation.player().select_response_stream(0x0012181A);
        if (player.vy < 8) {
            player.vy = static_cast<std::int16_t>(player.vy + 0x78);
        }
        player.grounded = false;
        break;
    }
    case 0x2D:  // TerrainHandler_BouncePlayerBlock (0x001B56B6)
        player.vx = static_cast<std::int16_t>(-0x400);
        player.vy = static_cast<std::int16_t>(0x200);
        services_.animation.player().select_response_stream(0x00121AD8);
        services_.interactions.start_bounce_response();
        break;
    case 0x30:  // TerrainHandler_LandingResponseBlock (0x001B537A)
        // The ROM subtracts 0x7C from PLAYER_VY, clears FFF0B0, arms the
        // landing state, then calls 0x001A99C6 to align PLAYER_X to the
        // current world tile plus eight pixels. The animation selector call
        // is intentionally left to the normal selector pass; the handler
        // itself writes no animation stream directly.
        player.vy = static_cast<std::int16_t>(player.vy - 0x7C);
        player.terrain_horizontal_response = 0;
        player.terrain_landing_state = 0xFF;
        player.terrain_response_timer_state = 0;
        player.grounded = false;
        player.x = ((world_x & ~0x0F) + 8) - camera.x;
        break;
    case 0x40:  // TerrainHandler_MovePlayerRight (0x001B536C)
        // Exact ROM body: ADDQ.W #8,PLAYER_X; CLR.W,FFF0B0.
        player.x += 8;
        player.terrain_horizontal_response = 0;
        break;
    case 0x41:  // TerrainHandler_HorizontalResponseBlock (0x001B53A2)
        // Exact ROM body: SUBQ.W #8,PLAYER_X; CLR.W,FFF0B0. There is no
        // native lower-bound clamp in this handler.
        player.x -= 8;
        player.terrain_horizontal_response = 0;
        break;
    case 0x47:  // TerrainHandler_ToggleSurfaceMode (0x001B5470)
        if (player.terrain_surface_latch == 0) {
            player.terrain_surface_latch = 0xFF;
            player.terrain_surface_mode ^= 1;
        }
        break;
    default:
        break;
    }
}

}  // namespace openaladdin
