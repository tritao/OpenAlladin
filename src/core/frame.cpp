#include "core/frame.hpp"

#include "core/animation_vm.hpp"
#include "core/camera.hpp"
#include "core/collision.hpp"
#include "core/level.hpp"
#include "core/movement_vm.hpp"
#include "core/player.hpp"
#include "core/terrain.hpp"
#include "core/trace.hpp"

namespace openaladdin::core {
namespace {

constexpr std::array<FrameService, kFrameServiceCount> kFrameServices = {{
    {1, 0x001A8C20, 0x001A91C6, "Frame_InputAndResourceService"},
    {2, 0x001A8C24, 0x001A8E0C, "Player_PublishWorldCoordinates"},
    {3, 0x001A8C28, 0x001AD7B4, "Player_TerrainLandingResolver"},
    {4, 0x001A8C2C, 0x001A8E0C, "Player_PublishWorldCoordinates"},
    {5, 0x001A8C30, 0x001AD632, "Player_TerrainEdgeProbe"},
    {6, 0x001A8C34, 0x001A986E, "Player_ApplyTerrainResponse"},
    {7, 0x001A8C38, 0x001A99F0, "Player_ResolveTerrainContact"},
    {8, 0x001A8C3C, 0x001ADE36, "MovementVM_TickActors"},
    {9, 0x001A8C40, 0x001ADB5C, "Actor_TerrainCollisionLoop"},
    {10, 0x001A8C44, 0x001ABB40, "Actor_PlayerCollisionPass"},
    {11, 0x001A8C50, 0x001B321C, "Terrain_TestQueryFlag1"},
    {12, 0x001A8C62, 0x001B3212, "Terrain_TestQueryFlag0"},
    {13, 0x001A8C74, 0x001B3226, "Terrain_TestQueryFlag2"},
    {14, 0x001A8C86, 0x001B3230, "Terrain_TestQueryFlag3"},
    {15, 0x001A8C92, 0x001B1E38, "Terrain_ResolvePlayerCell"},
    {16, 0x001A8C96, 0x001A9D98, "Player_TerrainResponseStateMachine"},
    {17, 0x001A8C9A, 0x001A9716, "Player_HandleJumpAndVerticalState"},
    {18, 0x001A8C9E, 0x001A8E0C, "Player_PublishWorldCoordinates"},
    {19, 0x001A8CA2, 0x001AA8FA, "Camera_UpdateFollow"},
    {20, 0x001A8CA6, 0x001A9304, "Player_SelectLocomotionOrAction"},
    {21, 0x001A8CAA, 0x001A9502, "Player_SelectActionAnimation"},
    {22, 0x001A8CAE, 0x001ABD7E, "Actor_ActorCollisionPass"},
    {23, 0x001A8CB2, 0x001B02EC, "Interaction_UpdateTarget"},
    {24, 0x001A8CB6, 0x001A8F0C, "Level_ExitAndTerminalTransition"},
    {25, 0x001A8CBA, 0x001A8F04, "Level_InvokeFrameCallback"},
    {26, 0x001A8CBE, 0x001B00CA, "Interaction_UpdateCounter"},
    {27, 0x001A8CC2, 0x001B01AC, "Interaction_UpdateResourceState"},
    {28, 0x001A8CC6, 0x001A8E0C, "Player_PublishWorldCoordinates"},
    {29, 0x001A8CCA, 0x001A8E3E, "SceneScript_AdvanceState"},
    {30, 0x001A8CCE, 0x001AC784, "AnimationVM_TickActors"},
    {31, 0x001A8CD2, 0x001AB7C4, "Render_BuildActorRecords"},
    {32, 0x001A8CD8, 0x001B249E, "Frame_WaitForVBlankWork"},
    {33, 0x001A8CDC, 0x001AC726, "Render_SubmitActorSprites"},
    {34, 0x001A8CE0, 0x001AB776, "Render_SubmitPlayerSprites"},
    {35, 0x001A8CE4, 0x001AE0F6, "SceneResource_StreamVdpRecord"},
    {36, 0x001A8CE8, 0x001AAA2A, "Camera_PublishScroll"},
    {37, 0x001A8CEE, 0x001B315C, "SceneScript_CompleteToState1"},
}};

constexpr FrameService kFrameEntry = {
    0, 0x001A8C16, 0x001A8C16, "Game_FrameUpdateLoop"};

}  // namespace

const std::array<FrameService, kFrameServiceCount>& frame_service_table() {
    return kFrameServices;
}

void reset(CoreRuntime& core) {
    core.ram.bytes.fill(0);
}

void bind_rom(CoreRuntime& core, RomView rom) {
    core.rom = rom;
}

void player_publish_world_coordinates(CoreRuntime& core, CoreTrace* trace) {
    const std::uint16_t world_x = static_cast<std::uint16_t>(
        read16(core.ram, kWorldCameraX) + read16(core.ram, kPlayerX));
    const std::uint16_t world_y = static_cast<std::uint16_t>(
        read16(core.ram, kWorldCameraY) + read16(core.ram, kPlayerY));
    write16(core.ram, kPlayerWorldX, world_x);
    write16(core.ram, kPlayerWorldY, world_y);
    if (trace != nullptr) {
        trace_write(*trace, kPlayerWorldX, 2, world_x,
                    "Player_PublishWorldCoordinates", 0x001A8E0C);
        trace_write(*trace, kPlayerWorldY, 2, world_y,
                    "Player_PublishWorldCoordinates", 0x001A8E0C);
    }
}

void step_frame(
    CoreRuntime& core,
    std::uint64_t frame_number,
    std::string_view input_token,
    CoreTrace* trace
) {
    step_frame(
        core, frame_number, CoreInput{}, input_token, trace);
}

void step_frame(
    CoreRuntime& core,
    std::uint64_t frame_number,
    const CoreInput& input,
    std::string_view input_token,
    CoreTrace* trace
) {
    if (trace != nullptr) {
        trace->phase_count = 0;
        trace->write_count = 0;
        trace->collision_contact_count = 0;
        trace->collision_player_handler = 0;
        trace->collision_actor_handler = 0;
        trace->interaction_selector = 0;
        trace->interaction_handler = 0;
        trace->interaction_index = 0;
        trace->interaction_spawn_slot = 0;
        trace->camera_callback = 0;
        trace->frame_callback = 0;
        trace->exit_callback = 0;
        trace->level_exit_vdp_control = 0;
        trace->level_event_dispatched = false;
        trace->level_event_command = 0;
        trace->level_event_arg0 = 0;
        trace->level_event_arg1 = 0;
        trace->level_event_handler = 0;
        trace->level08_vdp_record_count = 0;
        trace->level08_vdp_last_control = 0;
        trace->level08_vdp_last_data = 0;
        trace_phase(*trace, kFrameEntry);
    }

    const std::uint8_t phase = static_cast<std::uint8_t>(
        read8(core.ram, kFramePhaseCounter) + 1);
    write8(core.ram, kFramePhaseCounter, phase);

    for (const FrameService& service : kFrameServices) {
        if (trace != nullptr) trace_phase(*trace, service);
        if (service.ordinal == 1) {
            player_sample_input(core, input);
        } else if (service.ordinal == 19) {
            camera_update_follow(core);
        } else if (service.ordinal == 8) {
            movement_vm_tick_actors(core);
        } else if (service.ordinal == 10) {
            const CollisionPassResult result = player_collision_pass(core);
            if (trace != nullptr) {
                trace->collision_contact_count += result.contact_count;
                trace->collision_player_handler = result.dispatch.handler;
            }
        } else if (service.ordinal == 15) {
            (void)terrain_resolve_player_cell(core);
        } else if (service.ordinal == 17) {
            player_integrate_motion(core);
            player_handle_jump_and_vertical_state(core, input);
        } else if (service.ordinal == 20) {
            player_select_locomotion_or_action(core, input);
        } else if (service.ordinal == 21) {
            player_select_action_animation(core, input);
        } else if (service.ordinal == 22) {
            const CollisionPassResult result = actor_collision_pass(core);
            if (trace != nullptr) {
                trace->collision_contact_count += result.contact_count;
                trace->collision_actor_handler = result.dispatch.handler;
            }
        } else if (service.ordinal == 25) {
            level_invoke_frame_callback(core, trace);
        } else if (service.ordinal == 30) {
            animation_vm_tick_actors(core);
        } else if (service.ordinal == 36) {
            camera_publish_scroll(core, trace);
        }
        if (service.ordinal == 2 || service.ordinal == 4
            || service.ordinal == 18 || service.ordinal == 28) {
            player_publish_world_coordinates(core, trace);
        }
    }

    if (trace != nullptr) trace_state(*trace, core, frame_number, input_token, true);
}

}  // namespace openaladdin::core
