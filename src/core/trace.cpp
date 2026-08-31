#include "core/trace.hpp"

#include "core/terrain.hpp"
#include "core/collision.hpp"

#include <ostream>

namespace openaladdin::core {
namespace {

void json_string(std::ostream& output, std::string_view value) {
    output.put('"');
    for (const char character : value) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default: output.put(character); break;
        }
    }
    output.put('"');
}

void write_hex_record(
    std::ostream& output,
    const GenesisRam& ram,
    std::size_t slot
) {
    constexpr char digits[] = "0123456789abcdef";
    const ConstActorView actor = actor_view(ram, slot);
    output.put('"');
    for (std::size_t offset = 0; offset < kActorRecordSize; ++offset) {
        const std::uint8_t value = actor_read8(actor, offset);
        output.put(digits[value >> 4]);
        output.put(digits[value & 0x0F]);
    }
    output.put('"');
}

void write_ram_bytes(std::ostream& output, const GenesisRam& ram) {
    struct RawField {
        RamAddress address;
        std::uint8_t width;
    };
    constexpr std::array<RawField, 43> fields = {{
        {kPlayerX, 2}, {kPlayerY, 2}, {kWorldCameraX, 2}, {kWorldCameraY, 2},
        {kPlayerWorldX, 2}, {kPlayerWorldY, 2},
        {kCameraReferenceX, 2}, {kCameraReferenceY, 2},
        {kCameraHorizontalThreshold, 2}, {kCameraVerticalThreshold, 2},
        {kPlayerCameraPixelX, 2}, {kPlayerCameraPixelY, 2},
        {kCameraTileX, 2}, {kCameraTileY, 2},
        {kFramePhaseCounter, 1}, {kVBlankReadyLatch, 1},
        {kGameDifficultyMode, 1}, {kSceneState, 1},
        {kGameDifficultyCounter, 1}, {kActiveSceneEntryGate, 1},
        {kCameraScrollX, 2}, {kCameraScrollY, 2},
        {kGlobalPrngState, 4},
        {kActorVmCommandContinuation, 4},
        {kActorVmCursorClearContinuation, 4},
        {kActorVmMovementPass, 1},
        {kPlayerTerrainBrakeState, 1}, {kPlayerTerrainLandingState, 1},
        {kPlayerTerrainBehavior, 1}, {kSceneScriptPending, 1},
        {kSceneVdpUpdateFlag, 1}, {kSceneTransitionEvent, 1},
        {kCameraScrollLeftPending, 1}, {kCameraScrollRightPending, 1},
        {kCameraScrollUpPending, 1}, {kCameraScrollDownPending, 1},
        {kCameraUpdateDelay, 1}, {kCameraSpecialMode, 1},
        {kCameraScrollApplyGate, 1}, {kCameraScrollDataCursor, 4},
        {kInteractionRowPointer, 4}, {kInteractionHandlerX, 2},
        {kInteractionHandlerY, 2},
    }};
    output << "[";
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0) output.put(',');
        const RawField field = fields[index];
        output << "{\"address\":" << field.address
               << ",\"width\":" << static_cast<unsigned>(field.width)
               << ",\"value\":";
        if (field.width == 1) {
            output << static_cast<unsigned>(read8(ram, field.address));
        } else if (field.width == 2) {
            output << read16(ram, field.address);
        } else {
            output << read32(ram, field.address);
        }
        output << "}";
    }
    output << "]";
}

void write_actor(std::ostream& output, const GenesisRam& ram, std::size_t slot) {
    const ConstActorView actor = actor_view(ram, slot);
    output << "{\"slot\":" << slot
           << ",\"address\":" << actor_address(slot, 0)
           << ",\"type\":" << static_cast<unsigned>(actor_read8(actor, 0x00))
           << ",\"actor_timer\":" << static_cast<unsigned>(actor_read8(actor, 0x01))
           << ",\"x\":" << actor_read16(actor, 0x02)
           << ",\"y\":" << actor_read16(actor, 0x04)
           << ",\"facing_x_flip\":" << static_cast<unsigned>(actor_read8(actor, 0x09))
           << ",\"movement_pc\":" << actor_read32(actor, 0x0A)
           << ",\"movement_loop_pc\":" << actor_read32(actor, 0x0E)
           << ",\"movement_loop_timer\":" << static_cast<unsigned>(actor_read8(actor, 0x12))
           << ",\"frame_ptr\":" << actor_read32(actor, 0x14)
           << ",\"velocity_x\":" << read_i16(*actor.ram, actor_address(slot, 0x18))
           << ",\"velocity_y\":" << read_i16(*actor.ram, actor_address(slot, 0x1A))
           << ",\"animation_pc\":" << actor_read32(actor, 0x20)
           << ",\"resource_count\":" << static_cast<unsigned>(actor_read8(actor, kActorResourceCountOffset))
           << ",\"resource_pointer\":" << actor_read32(actor, kActorResourcePointerOffset)
           << ",\"sprite_vram_base\":" << actor_read32(actor, kActorSpriteVramBaseOffset)
           << ",\"facing_y_flip\":" << static_cast<unsigned>(actor_read8(actor, 0x35))
           << ",\"movement_command_timer\":" << static_cast<unsigned>(actor_read8(actor, 0x36))
           << ",\"animation_timer\":" << static_cast<unsigned>(actor_read8(actor, 0x37))
           << ",\"movement_return_pc\":" << actor_read32(actor, 0x38)
           << ",\"flags\":" << static_cast<unsigned>(actor_read8(actor, 0x3C))
           << ",\"terrain_response\":" << static_cast<unsigned>(actor_read8(actor, kActorTerrainResponseOffset))
           << ",\"linked_record_pointer\":" << actor_read32(actor, kActorLinkedRecordPointerOffset)
           << ",\"record\":";
    write_hex_record(output, ram, slot);
    output << "}";
}

void write_vm(std::ostream& output, const GenesisRam& ram, std::size_t slot) {
    const ConstActorView actor = actor_view(ram, slot);
    output << "{\"slot\":" << slot
           << ",\"animation_pc\":" << actor_read32(actor, 0x20)
           << ",\"animation_timer\":" << static_cast<unsigned>(actor_read8(actor, 0x37))
           << ",\"movement_pc\":" << actor_read32(actor, 0x0A)
           << ",\"movement_loop_pc\":" << actor_read32(actor, 0x0E)
           << ",\"movement_loop_timer\":" << static_cast<unsigned>(actor_read8(actor, 0x12))
           << ",\"movement_command_timer\":" << static_cast<unsigned>(actor_read8(actor, 0x36))
           << ",\"movement_return_pc\":" << actor_read32(actor, 0x38)
           << "}";
}

}  // namespace

void trace_begin(
    CoreTrace& trace,
    std::ostream& output,
    const CoreRuntime* core
) {
    trace.output = &output;
    trace.phase_count = 0;
    trace.write_count = 0;
    trace.collision_contact_count = 0;
    trace.collision_player_handler = 0;
    trace.collision_actor_handler = 0;
    trace.interaction_selector = 0;
    trace.interaction_handler = 0;
    trace.interaction_index = 0;
    trace.interaction_spawn_slot = 0;
    trace.frame_atomic = false;
    output << "{\"type\":\"header\",\"format\":\"openaladdin-core-trace-v1\""
           << ",\"state_boundary\":\"game-loop\""
           << ",\"frame_contract\":\"S[N] = synchronized state at boundary N; I[N] = input for S[N] -> S[N+1]\""
           << ",\"rom\":\"genesis\",\"ram_start\":" << kWorkRamBase
           << ",\"ram_size\":" << kWorkRamSize
           << ",\"rom_size\":" << (core == nullptr ? 0 : core->rom.size)
           << ",\"actor_table_base\":" << kActorTableBase
           << ",\"actor_stride\":" << kActorRecordSize
           << ",\"actor_slot_count\":" << kActorSlotCount
           << ",\"sync\":{\"boundary\":\"Game_FrameUpdateLoop\",\"actors_qualified\":true"
           << ",\"atomic_fields\":[\"player\",\"camera\",\"terrain\",\"scene\",\"actors\"]"
           << ",\"actor_slot_count\":" << kActorSlotCount << "}}\n";
}

void trace_phase(CoreTrace& trace, const FrameService& phase) {
    if (trace.phase_count < trace.phases.size()) {
        trace.phases[trace.phase_count++] = phase;
    }
}

void trace_write(
    CoreTrace& trace,
    RamAddress address,
    std::uint8_t width,
    std::uint32_t value,
    const char* phase,
    RamAddress rom_entry_pc
) {
    if (trace.write_count >= trace.writes.size()) return;
    trace.writes[trace.write_count++] = TraceWrite{
        address, width, value, phase, rom_entry_pc};
}

void trace_state(
    CoreTrace& trace,
    const CoreRuntime& core,
    std::uint64_t frame_number,
    std::string_view input_token,
    bool atomic
) {
    if (trace.output == nullptr) return;
    std::ostream& output = *trace.output;
    const GenesisRam& ram = core.ram;
    trace.frame_atomic = atomic;
    output << "{\"type\":\"state\",\"format\":\"openaladdin-core-trace-v1\""
           << ",\"frame\":" << frame_number << ",\"pc\":" << 0x001A8C16
           << ",\"input\":";
    json_string(output, input_token);
    output << ",\"input_role\":\"I[N]: transition from S[N] to S[N+1]\""
           << ",\"player\":{\"x\":" << read_i16(ram, kPlayerX)
           << ",\"y\":" << read_i16(ram, kPlayerY)
           << ",\"world_x\":" << read16(ram, kPlayerWorldX)
           << ",\"world_y\":" << read16(ram, kPlayerWorldY)
           << ",\"vx\":" << read_i16(ram, kPlayerVelocityX)
           << ",\"vy\":" << read_i16(ram, kPlayerVelocityY)
           << ",\"animation_pc\":" << read32(ram, kPlayerAnimationPc)
           << ",\"frame_ptr\":" << read32(ram, kPlayerFramePointer)
           << ",\"facing_x_flip\":" << static_cast<unsigned>(read8(ram, kPlayerFacingXFlip))
           << ",\"animation_timer\":" << static_cast<unsigned>(read8(ram, kPlayerAnimationTimer))
           << ",\"actor_flags\":" << static_cast<unsigned>(read8(ram, kPlayerFlags))
           << ",\"action_response_field\":" << static_cast<unsigned>(read8(ram, kPlayerActionResponseField))
           << ",\"action_response_state_b\":" << static_cast<unsigned>(read8(ram, kPlayerActionResponseStateB))
           << ",\"action_animation_state\":" << static_cast<unsigned>(read8(ram, kPlayerActionAnimationState))
           << ",\"interaction_pending\":" << static_cast<unsigned>(read8(ram, kPlayerInteractionPending))
           << ",\"terrain_brake_state\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainBrakeState))
           << "}"
           << ",\"camera\":{\"x\":" << read16(ram, kWorldCameraX)
           << ",\"y\":" << read16(ram, kWorldCameraY)
           << ",\"reference_x\":" << read16(ram, kCameraReferenceX)
           << ",\"reference_y\":" << read16(ram, kCameraReferenceY)
           << ",\"horizontal_threshold\":" << read16(ram, kCameraHorizontalThreshold)
           << ",\"vertical_threshold\":" << read16(ram, kCameraVerticalThreshold)
           << ",\"scroll_x\":" << read_i16(ram, kCameraScrollX)
           << ",\"scroll_y\":" << read_i16(ram, kCameraScrollY)
           << ",\"scroll_left_pending\":" << static_cast<unsigned>(read8(ram, kCameraScrollLeftPending))
           << ",\"scroll_right_pending\":" << static_cast<unsigned>(read8(ram, kCameraScrollRightPending))
           << ",\"scroll_up_pending\":" << static_cast<unsigned>(read8(ram, kCameraScrollUpPending))
           << ",\"scroll_down_pending\":" << static_cast<unsigned>(read8(ram, kCameraScrollDownPending))
           << ",\"update_delay\":" << static_cast<unsigned>(read8(ram, kCameraUpdateDelay))
           << ",\"special_mode\":" << static_cast<unsigned>(read8(ram, kCameraSpecialMode))
           << ",\"scroll_apply_gate\":" << static_cast<unsigned>(read8(ram, kCameraScrollApplyGate))
           << ",\"pixel_x\":" << read16(ram, kPlayerCameraPixelX)
           << ",\"pixel_y\":" << read16(ram, kPlayerCameraPixelY)
           << ",\"tile_x\":" << read16(ram, kCameraTileX)
           << ",\"tile_y\":" << read16(ram, kCameraTileY)
           << ",\"scroll_data_cursor\":" << read32(ram, kCameraScrollDataCursor)
           << "}"
           << ",\"scene\":{\"state\":" << static_cast<unsigned>(read8(ram, kSceneState))
           << ",\"script_cursor\":" << read32(ram, kSceneScriptCursor)
           << ",\"script_data_cursor\":" << read32(ram, kSceneScriptData)
           << ",\"table_index\":" << static_cast<unsigned>(read8(ram, kSceneTableIndex))
           << ",\"script_pending\":" << static_cast<unsigned>(read8(ram, kSceneScriptPending))
           << ",\"resource_status\":" << static_cast<unsigned>(read8(ram, kSceneResourceStatus))
           << ",\"resource_error\":" << static_cast<unsigned>(read8(ram, kSceneResourceError))
           << ",\"vdp_update\":" << static_cast<unsigned>(read8(ram, kSceneVdpUpdateFlag))
           << ",\"vdp_clear\":" << static_cast<unsigned>(read8(ram, kSceneVdpClearFlag))
           << ",\"transition_event\":" << static_cast<unsigned>(read8(ram, kSceneTransitionEvent))
           << "}"
           << ",\"terrain\":{\"query_result\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainQueryResult))
           << ",\"query_input_raw\":" << static_cast<unsigned>(read8(ram, kTerrainQueryInputRaw))
           << ",\"push_right\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainPushRight))
           << ",\"push_left\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainPushLeft))
           << ",\"push_up\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainPushUp))
           << ",\"push_down\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainPushDown))
           << ",\"behavior\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainBehavior))
           << ",\"surface_mode\":" << read16(ram, kPlayerTerrainSurfaceMode)
           << ",\"surface_latch\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainSurfaceLatch))
           << ",\"stop_left_motion\":" << static_cast<unsigned>(read8(ram, kTerrainStopLeftMotion))
           << ",\"stop_right_motion\":" << static_cast<unsigned>(read8(ram, kTerrainStopRightMotion))
           << ",\"stop_upward_motion\":" << static_cast<unsigned>(read8(ram, kTerrainStopUpwardMotion))
           << ",\"horizontal_response\":" << read_i16(ram, kPlayerTerrainHorizontalResponse)
           << ",\"response_active\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainResponseActive))
           << ",\"jump_response_counter\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainJumpResponseCounter))
           << ",\"vertical_stop\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainVerticalStop))
           << ",\"landing_state\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainLandingState))
           << ",\"response_timer_state\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainResponseTimer))
           << ",\"state\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainState))
           << ",\"response_latch\":" << static_cast<unsigned>(read8(ram, kPlayerTerrainResponseLatch))
           << ",\"handler_pc\":" << terrain_handler_for_behavior(
               read8(ram, kPlayerTerrainBehavior))
           << "}"
           << ",\"collision\":{\"contact_count\":"
           << trace.collision_contact_count
           << ",\"player_handler_pc\":" << trace.collision_player_handler
           << ",\"actor_handler_pc\":" << trace.collision_actor_handler
           << ",\"current_actor_type\":"
           << static_cast<unsigned>(read8(ram, kPlayerCollisionCurrentActorType))
           << ",\"response_suppress\":"
           << static_cast<unsigned>(read8(ram, kPlayerCollisionResponseSuppress))
           << "}"
           << ",\"interaction\":{\"selector\":"
           << static_cast<unsigned>(trace.interaction_selector)
           << ",\"handler_pc\":" << trace.interaction_handler
           << ",\"index\":" << trace.interaction_index
           << ",\"spawn_slot\":" << trace.interaction_spawn_slot
           << "}"
           << ",\"ram_bytes\":";
    write_ram_bytes(output, ram);
    output << ",\"actors\":[";
    for (std::size_t slot = 0; slot < kActorSlotCount; ++slot) {
        if (slot != 0) output.put(',');
        write_actor(output, ram, slot);
    }
    output << "]"
           << ",\"player_vm\":";
    write_vm(output, ram, 0);
    output << ",\"actor_vms\":[";
    for (std::size_t slot = 0; slot < kActorSlotCount; ++slot) {
        if (slot != 0) output.put(',');
        write_vm(output, ram, slot);
    }
    output << "]"
           << ",\"scheduler\":{\"frame_phase\":"
           << static_cast<unsigned>(read8(ram, kFramePhaseCounter))
           << ",\"phase_order\":[";
    for (std::size_t index = 0; index < trace.phase_count; ++index) {
        if (index != 0) output.put(',');
        json_string(output, trace.phases[index].name);
    }
    output << "],\"phase_pcs\":[";
    for (std::size_t index = 0; index < trace.phase_count; ++index) {
        if (index != 0) output.put(',');
        output << trace.phases[index].rom_entry_pc;
    }
    output << "],\"publication_writes\":[";
    for (std::size_t index = 0; index < trace.write_count; ++index) {
        if (index != 0) output.put(',');
        const TraceWrite& write = trace.writes[index];
        output << "{\"phase\":";
        json_string(output, write.phase);
        output << ",\"rom_entry_pc\":" << write.rom_entry_pc
               << ",\"address\":" << write.address
               << ",\"width\":" << static_cast<unsigned>(write.width)
               << ",\"value\":" << write.value << "}";
    }
    output << "]}"
           << ",\"capture\":{\"boundary\":\"Game_FrameUpdateLoop\",\"atomic\":"
           << (atomic ? "true" : "false")
           << ",\"atomic_fields\":[\"player\",\"camera\",\"terrain\",\"scene\",\"actors\"]"
           << ",\"atomic_actor_fields\":[\"type\",\"x\",\"y\",\"frame_ptr\",\"animation_pc\",\"movement_pc\",\"movement_loop_pc\",\"movement_loop_timer\",\"animation_timer\",\"movement_return_pc\",\"flags\",\"movement_command_timer\"]}}\n";
}

}  // namespace openaladdin::core
