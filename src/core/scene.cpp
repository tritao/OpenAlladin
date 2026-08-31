#include "core/scene.hpp"

#include "core/actor.hpp"
#include "core/level.hpp"
#include "core/ram.hpp"
#include "core/rom.hpp"
#include "core/trace.hpp"

namespace openaladdin::core {

namespace {

constexpr RamAddress kSceneResourceCommandTable = 0x000049D8;

bool rom_can_read(RomView rom, RamAddress address, std::size_t count) {
    if (!rom_is_bound(rom)) return false;
    const std::size_t offset = static_cast<std::size_t>(address);
    return offset <= rom.size && count <= rom.size - offset;
}

bool read_stream8(
    RomView rom,
    RamAddress& cursor,
    std::uint8_t& value
) {
    if (!rom_can_read(rom, cursor, 1)) return false;
    value = rom_read8(rom, cursor);
    cursor += 1;
    return true;
}

bool read_stream16(
    RomView rom,
    RamAddress& cursor,
    std::uint16_t& value
) {
    if (!rom_can_read(rom, cursor, 2)) return false;
    value = rom_read16(rom, cursor);
    cursor += 2;
    return true;
}

bool read_stream32(
    RomView rom,
    RamAddress& cursor,
    std::uint32_t& value
) {
    if (!rom_can_read(rom, cursor, 4)) return false;
    value = rom_read32(rom, cursor);
    cursor += 4;
    return true;
}

bool read_stream24(
    RomView rom,
    RamAddress& cursor,
    std::uint32_t& value
) {
    if (!rom_can_read(rom, cursor, 3)) return false;
    value = (static_cast<std::uint32_t>(rom_read8(rom, cursor)) << 16)
        | (static_cast<std::uint32_t>(rom_read8(rom, cursor + 1)) << 8)
        | rom_read8(rom, cursor + 2);
    cursor += 3;
    return true;
}

std::uint16_t increment_low_byte(std::uint16_t value) {
    return static_cast<std::uint16_t>(
        (value & 0xFF00U) | static_cast<std::uint8_t>(value + 1));
}

void publish_scene_resource_trace(
    const SceneResourceRunResult& result,
    CoreTrace* trace
) {
    if (trace == nullptr) return;
    trace->scene_resource_processed = true;
    trace->scene_resource_status = static_cast<std::uint8_t>(result.status);
    trace->scene_resource_last_command = result.last_command;
    trace->scene_resource_last_handler = result.last_handler;
    trace->scene_resource_last_vdp_control = result.last_vdp_control;
    trace->scene_resource_last_vdp_data = result.last_vdp_data;
    trace->scene_resource_last_vram_address = result.last_vram_address;
    trace->scene_resource_cursor = result.cursor;
    trace->scene_resource_stream_pointer = result.stream_pointer;
    trace->scene_resource_c000_source = result.c000_source;
    trace->scene_resource_tile_x = result.tile_x;
    trace->scene_resource_tile_y = result.tile_y;
    trace->scene_resource_tile_base = result.tile_base;
    trace->scene_resource_last_tile_x = result.last_tile_x;
    trace->scene_resource_last_tile_y = result.last_tile_y;
    trace->scene_resource_last_tile_row = result.last_tile_row;
    trace->scene_resource_instruction_count = result.instruction_count;
    trace->scene_resource_tile_write_count = result.tile_write_count;
    trace->scene_resource_service_frame_count = result.service_frame_count;
    trace->scene_resource_c000_load_requested = result.c000_load_requested;
    trace->scene_resource_frame_palette_prepare_requested =
        result.frame_palette_prepare_requested;
    trace->scene_resource_presentation_scratch_observed =
        result.presentation_scratch_observed;
    trace->scene_resource_actor_spawned = result.actor_spawned;
    trace->scene_resource_actor_spawn_slot = result.actor_spawn_slot;
}

}  // namespace

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

SceneResourceRunResult scene_resource_process_command_stream(
    CoreRuntime& core,
    RamAddress stream,
    std::uint16_t initial_x,
    std::uint16_t initial_y,
    const SceneResourceEffects& effects,
    CoreTrace* trace,
    std::size_t instruction_budget
) {
    SceneResourceRunResult result;
    RamAddress cursor = stream;
    std::uint16_t tile_x = initial_x;
    std::uint16_t tile_y = initial_y;
    result.c000_source = read32(core.ram, kSceneResourceC000Source);

    const auto finish = [&]() {
        result.cursor = cursor;
        result.tile_x = tile_x;
        result.tile_y = tile_y;
        result.tile_base = read16(core.ram, kSceneResourceTileBase);
        publish_scene_resource_trace(result, trace);
        return result;
    };

    const auto emit_tile = [&](std::uint16_t x, std::uint16_t y,
                               std::uint8_t tile_row) {
        result.last_tile_x = x;
        result.last_tile_y = y;
        result.last_tile_row = tile_row;
        ++result.tile_write_count;
        const VdpTileWriteResult vdp_write = vdp_write_tile_word(
            core.ram, core.vdp, x, y, tile_row);
        result.last_vdp_control = vdp_write.control;
        result.last_vdp_data = vdp_write.data;
        result.last_vram_address = vdp_write.vram_address;
        if (effects.write_tile != nullptr) {
            effects.write_tile(effects.context, SceneResourceTileWrite{
                x, y, tile_row, read16(core.ram, kSceneResourceTileBase),
                vdp_write.control, vdp_write.data, vdp_write.vram_address});
        }
    };

    const auto emit_tile_run = [&](std::uint16_t width,
                                   std::uint16_t height,
                                   bool advance_y,
                                   bool restore_x_each_row,
                                   std::uint8_t tile_row) {
        for (std::uint16_t row = 0; row < height; ++row) {
            const std::uint16_t row_x = tile_x;
            for (std::uint16_t column = 0; column < width; ++column) {
                emit_tile(tile_x, tile_y, tile_row);
                tile_x = increment_low_byte(tile_x);
            }
            if (restore_x_each_row) tile_x = row_x;
            if (advance_y) tile_y = increment_low_byte(tile_y);
        }
    };

    if (read8(core.ram, kSceneResourceStatus) != 0) {
        result.status = SceneResourceRunStatus::StatusChanged;
        return finish();
    }

    for (std::size_t instruction = 0;
         instruction < instruction_budget;
         ++instruction) {
        if (read8(core.ram, kSceneResourceStatus) != 0) {
            result.status = SceneResourceRunStatus::StatusChanged;
            return finish();
        }

        std::uint8_t command = 0;
        if (!read_stream8(core.rom, cursor, command)) {
            result.status = SceneResourceRunStatus::InvalidStream;
            return finish();
        }
        result.last_command = command;
        ++result.instruction_count;

        if (command < 0x10) {
            const RamAddress table_entry = kSceneResourceCommandTable
                + static_cast<RamAddress>(command) * 4;
            result.last_handler = rom_can_read(core.rom, table_entry, 4)
                ? rom_read32(core.rom, table_entry) : 0;
        } else if (command < 0x20) {
            result.status = SceneResourceRunStatus::InvalidStream;
            return finish();
        }

        switch (command) {
        case 0x00:
            result.status = SceneResourceRunStatus::Finished;
            return finish();

        case 0x01: {
            std::uint8_t raw_delta = 0;
            if (!read_stream8(core.rom, cursor, raw_delta)) {
                result.status = SceneResourceRunStatus::InvalidStream;
                return finish();
            }
            tile_x = static_cast<std::uint16_t>(
                tile_x + static_cast<std::int16_t>(
                    static_cast<std::int8_t>(raw_delta)));
            break;
        }

        case 0x02: {
            std::uint8_t raw_delta = 0;
            if (!read_stream8(core.rom, cursor, raw_delta)) {
                result.status = SceneResourceRunStatus::InvalidStream;
                return finish();
            }
            tile_y = static_cast<std::uint16_t>(
                tile_y + static_cast<std::int16_t>(
                    static_cast<std::int8_t>(raw_delta)));
            break;
        }

        case 0x03:
        case 0x04: {
            std::uint8_t count_byte = 0;
            std::uint8_t raw_tile = 0;
            if (!read_stream8(core.rom, cursor, count_byte)
                || !read_stream8(core.rom, cursor, raw_tile)) {
                result.status = SceneResourceRunStatus::InvalidStream;
                return finish();
            }
            const std::uint16_t count = count_byte == 0 ? 0x100 : count_byte;
            emit_tile_run(
                command == 0x03 ? count : 1,
                command == 0x03 ? 1 : count,
                command == 0x04,
                command == 0x04,
                static_cast<std::uint8_t>(raw_tile - 0x20U));
            break;
        }

        case 0x05: {
            std::uint8_t width_byte = 0;
            std::uint8_t height_byte = 0;
            std::uint8_t raw_tile = 0;
            if (!read_stream8(core.rom, cursor, width_byte)
                || !read_stream8(core.rom, cursor, height_byte)
                || !read_stream8(core.rom, cursor, raw_tile)) {
                result.status = SceneResourceRunStatus::InvalidStream;
                return finish();
            }
            const std::uint16_t width = width_byte == 0 ? 0x100 : width_byte;
            const std::uint16_t height = height_byte == 0 ? 0x100 : height_byte;
            emit_tile_run(
                width, height, true, true,
                static_cast<std::uint8_t>(raw_tile - 0x20U));
            break;
        }

        case 0x06: {
            std::uint8_t frame_byte = 0;
            if (!read_stream8(core.rom, cursor, frame_byte)) {
                result.status = SceneResourceRunStatus::InvalidStream;
                return finish();
            }
            const std::uint16_t frames = frame_byte == 0 ? 0x100 : frame_byte;
            result.service_frame_count = static_cast<std::uint16_t>(
                result.service_frame_count + frames);
            for (std::uint16_t frame = 0; frame < frames; ++frame) {
                if (effects.service_frame != nullptr) {
                    effects.service_frame(effects.context);
                }
                if (read8(core.ram, kSceneResourceStatus) != 0) {
                    result.status = SceneResourceRunStatus::StatusChanged;
                    return finish();
                }
            }
            break;
        }

        case 0x07:
            tile_y = increment_low_byte(tile_y);
            tile_x = static_cast<std::uint16_t>(tile_x & 0xFF00U);
            break;

        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B:
            write16(core.ram, kSceneResourceTileBase,
                    static_cast<std::uint16_t>((command - 0x08U) * 0x2000U));
            break;

        case 0x0C: {
            std::uint32_t pointer = 0;
            if (!read_stream24(core.rom, cursor, pointer)) {
                result.status = SceneResourceRunStatus::InvalidStream;
                return finish();
            }
            result.stream_pointer = pointer;
            break;
        }

        case 0x0D:
            result.c000_load_requested = true;
            if (result.c000_source == 0) {
                vdp_clear_vram_c000(core.vdp);
            }
            if (effects.load_or_clear_c000 != nullptr) {
                effects.load_or_clear_c000(effects.context, result.c000_source);
            }
            break;

        case 0x0E:
            result.frame_palette_prepare_requested = true;
            vdp_clear_vram_c000(core.vdp);
            if (effects.prepare_frame_and_palette != nullptr) {
                effects.prepare_frame_and_palette(effects.context);
            }
            break;

        case 0x0F: {
            std::uint32_t template_address = 0;
            std::uint16_t x = 0;
            std::uint16_t y = 0;
            if (!read_stream32(core.rom, cursor, template_address)
                || !read_stream16(core.rom, cursor, x)
                || !read_stream16(core.rom, cursor, y)) {
                result.status = SceneResourceRunStatus::InvalidStream;
                return finish();
            }
            if (!rom_can_read(core.rom, template_address, 0x13)) {
                result.status = SceneResourceRunStatus::InvalidStream;
                return finish();
            }
            const auto slot = actor_find_free_slot(
                core.ram, ActorAllocationPool::CommonForward);
            if (slot && actor_initialize_from_template(
                    core, *slot, template_address)) {
                const ActorView actor = actor_view(core.ram, *slot);
                actor_write16(actor, kActorXOffset, x);
                actor_write16(actor, kActorYOffset, y);
                result.actor_spawned = true;
                result.actor_spawn_slot = *slot;
            }
            break;
        }

        default:
            break;
        }

        if (command >= 0x20) {
            if (read8(core.ram, kSceneResourcePresentationScratch) != 0) {
                result.presentation_scratch_observed = true;
                if (effects.object_command_noop_hook != nullptr) {
                    effects.object_command_noop_hook(effects.context);
                }
            }
            emit_tile(tile_x, tile_y,
                      static_cast<std::uint8_t>(command - 0x20U));
            tile_x = increment_low_byte(tile_x);
        }
    }

    result.status = SceneResourceRunStatus::BudgetExhausted;
    return finish();
}

SceneResourceRunResult scene_resource_process_command_stream_with_presentation_scratch(
    CoreRuntime& core,
    RamAddress stream,
    std::uint16_t initial_x,
    std::uint16_t initial_y,
    const SceneResourceEffects& effects,
    CoreTrace* trace,
    std::size_t instruction_budget
) {
    write8(core.ram, kSceneResourcePresentationScratch, 1);
    const SceneResourceRunResult result = scene_resource_process_command_stream(
        core, stream, initial_x, initial_y, effects, trace,
        instruction_budget);
    write8(core.ram, kSceneResourcePresentationScratch, 0);
    return result;
}

SceneResourceStreamsResult scene_resource_process_command_streams(
    CoreRuntime& core,
    const SceneResourceEffects& effects,
    CoreTrace* trace,
    std::size_t instruction_budget
) {
    constexpr RamAddress kTransitionLabelOn = 0x00126512;
    constexpr RamAddress kTransitionLabelOff = 0x00126516;

    const RamAddress first_stream = read8(core.ram, kSceneTransitionEvent) != 0
        ? kTransitionLabelOff : kTransitionLabelOn;
    const RamAddress second_stream = read8(core.ram, kSceneVdpUpdateFlag) != 0
        ? kTransitionLabelOff : kTransitionLabelOn;
    SceneResourceStreamsResult result;
    result.first = scene_resource_process_command_stream(
        core, first_stream, 0x0015, 0x0010, effects, trace,
        instruction_budget);
    result.second = scene_resource_process_command_stream(
        core, second_stream, 0x0019, 0x0012, effects, trace,
        instruction_budget);
    return result;
}

SceneResourceActorRunResult scene_resource_instantiate_actors(
    CoreRuntime& core,
    const SceneResourceEffects& effects,
    CoreTrace* trace,
    std::size_t instruction_budget
) {
    constexpr RamAddress kSceneResourceSetupStream = 0x00126692;
    constexpr RamAddress kSceneResourceModeTable = 0x00126D7E;
    constexpr RamAddress kSceneResourceObjectHandlerTable = 0x00004A18;
    constexpr RamAddress kSceneResourceActorTemplate = 0x001B7968;
    constexpr RamAddress kSceneResourceActorMovement = 0x0011F728;

    SceneResourceActorRunResult result;
    result.setup_stream = scene_resource_process_command_stream(
        core, kSceneResourceSetupStream, 0x000A, 0x000E, effects, trace,
        instruction_budget);

    const std::size_t mode_entry = static_cast<std::size_t>(
        read8(core.ram, kSceneResourceMode)) * 4;
    if (!rom_can_read(core.rom, kSceneResourceModeTable
                          + static_cast<RamAddress>(mode_entry), 4)) {
        result.final_x = result.initial_x;
        result.final_y = result.initial_y;
        return result;
    }
    const RamAddress record = rom_read32(
        core.rom, kSceneResourceModeTable
            + static_cast<RamAddress>(mode_entry));
    if (!rom_can_read(core.rom, record, 12)) return result;
    result.selected_record = record;
    result.object_stream = rom_read32(core.rom, record);
    result.initial_x = rom_read16(core.rom, record + 4);
    result.initial_y = rom_read16(core.rom, record + 6);
    result.final_x = result.initial_x;
    result.final_y = result.initial_y;
    write32(core.ram, kSceneResourceActorResource,
            rom_read32(core.rom, record + 8));
    write8(core.ram, kSceneResourceActorXAdvance, 0x11);

    const auto publish_object_trace = [&]() {
        if (trace == nullptr) return;
        trace->scene_resource_object_stream = result.object_stream;
        trace->scene_resource_last_object_handler =
            result.last_object_handler;
        trace->scene_resource_object_command_count =
            result.object_command_count;
        trace->scene_resource_object_actor_count = result.actor_count;
        trace->scene_resource_last_object_actor_slot =
            result.last_actor_slot;
        trace->scene_resource_object_presentation_scratch_observed =
            result.presentation_scratch_observed;
    };

    RamAddress cursor = result.object_stream;
    std::uint16_t x = result.initial_x;
    std::uint16_t y = result.initial_y;
    for (std::size_t instruction = 0;
         instruction < instruction_budget;
         ++instruction) {
        std::uint8_t command = 0;
        if (!read_stream8(core.rom, cursor, command)) return result;
        if (command == 0x20) {
            result.final_x = x;
            result.final_y = y;
            result.valid = true;
            publish_object_trace();
            return result;
        }
        if (command < 0x20) {
            switch (command) {
            case 0x00:
                result.final_x = x;
                result.final_y = y;
                result.valid = true;
                publish_object_trace();
                return result;
            case 0x01: {
                std::uint8_t raw_delta = 0;
                if (!read_stream8(core.rom, cursor, raw_delta)) return result;
                x = static_cast<std::uint16_t>(
                    x + static_cast<std::int16_t>(
                        static_cast<std::int8_t>(raw_delta)));
                continue;
            }
            case 0x02: {
                std::uint8_t raw_delta = 0;
                if (!read_stream8(core.rom, cursor, raw_delta)) return result;
                y = static_cast<std::uint16_t>(
                    y + static_cast<std::int16_t>(
                        static_cast<std::int8_t>(raw_delta)));
                continue;
            }
            case 0x07:
                y = increment_low_byte(y);
                x = static_cast<std::uint16_t>(x & 0xFF00U);
                continue;
            case 0x08:
            case 0x09:
            case 0x0A:
            case 0x0B:
                write16(core.ram, kSceneResourceTileBase,
                        static_cast<std::uint16_t>((command - 0x08U)
                                                   * 0x2000U));
                continue;
            case 0x0C: {
                std::uint32_t pointer = 0;
                if (!read_stream24(core.rom, cursor, pointer)) return result;
                result.setup_stream.stream_pointer = pointer;
                continue;
            }
            default:
                publish_object_trace();
                return result;
            }
        }

        ++result.object_command_count;
        const std::uint8_t object_index = static_cast<std::uint8_t>(
            command - 0x20U);
        const RamAddress handler_address = kSceneResourceObjectHandlerTable
            + static_cast<RamAddress>(object_index) * 4;
        result.last_object_handler = rom_can_read(
            core.rom, handler_address, 4) ? rom_read32(
                core.rom, handler_address) : 0;
        if (read8(core.ram, kSceneResourcePresentationScratch) != 0) {
            result.presentation_scratch_observed = true;
            if (effects.object_command_noop_hook != nullptr) {
                effects.object_command_noop_hook(effects.context);
            }
        }
        if (result.last_object_handler != 0) {
            const auto slot = actor_find_free_slot(
                core.ram, ActorAllocationPool::SceneResourceForward);
            if (!slot || !actor_initialize_from_template(
                    core, *slot, kSceneResourceActorTemplate)) {
                result.final_x = x;
                result.final_y = y;
                publish_object_trace();
                return result;
            }
            const ActorView actor = actor_view(core.ram, *slot);
            actor_write32(actor, kActorAnimationPcOffset,
                          result.last_object_handler);
            actor_write32(actor, kActorMovementPcOffset,
                          kSceneResourceActorMovement);
            actor_write16(actor, kActorXOffset, x);
            actor_write16(actor, kActorYOffset, y);
            ++result.actor_count;
            result.last_actor_slot = *slot;
        }
        x = static_cast<std::uint16_t>(
            x + read8(core.ram, kSceneResourceActorXAdvance));
    }

    result.final_x = x;
    result.final_y = y;
    publish_object_trace();
    return result;
}

SceneResourcePresentationResult scene_resource_run_state_presentation(
    CoreRuntime& core,
    std::uint8_t scene_state,
    bool secondary,
    const SceneResourceEffects& effects,
    CoreTrace* trace,
    std::size_t instruction_budget
) {
    SceneResourcePresentationResult result;
    SceneResourcePresentationSpec& spec = result.spec;
    spec.scene_state = scene_state;
    spec.secondary = secondary;

    // These are the direct wrapper identities and fixed stream parameters
    // recovered from the 0x001B4BB8-0x001B4DF7 presentation family.
    switch (scene_state) {
    case 0x00:
        spec.loader_pc = 0x001B49DA;
        spec.c000_source = 0x0012D0FA;
        spec.palette_band0 = 0x00129952;
        spec.palette_band1 = 0x00129A92;
        spec.command_stream = 0x00127338;
        spec.initial_x = 0x0013;
        spec.initial_y = 0x0005;
        spec.wrapper_pc = 0x001B4C28;
        break;
    case 0x01:
        if (secondary) {
            spec.loader_pc = 0x001B4A2A;
            spec.c000_source = 0x0012E176;
            spec.palette_band0 = 0x00129992;
            spec.palette_band1 = 0x00129A92;
            spec.command_stream = 0x00127134;
            spec.initial_x = 0x0002;
            spec.initial_y = 0x000A;
            spec.wrapper_pc = 0x001B4BDC;
        } else {
            spec.loader_pc = 0x001B4A52;
            spec.c000_source = 0x0012E34A;
            spec.palette_band0 = 0x001299B2;
            spec.palette_band1 = 0x00129A92;
            spec.command_stream = 0x001270A8;
            spec.initial_x = 0x0015;
            spec.initial_y = 0x0008;
            spec.wrapper_pc = 0x001B4BB8;
        }
        break;
    case 0x03:
        spec.loader_pc = 0x001B49DA;
        spec.c000_source = 0x0012D0FA;
        spec.palette_band0 = 0x00129952;
        spec.palette_band1 = 0x00129A92;
        spec.command_stream = 0x00127207;
        spec.initial_x = 0x0011;
        spec.initial_y = 0x0005;
        spec.wrapper_pc = 0x001B4C02;
        break;
    case 0x04:
        spec.loader_pc = 0x001B49B2;
        spec.c000_source = 0x0012DA04;
        spec.palette_band0 = 0x00129932;
        spec.palette_band1 = 0x00129AB2;
        spec.command_stream = 0x001273E9;
        spec.initial_x = 0x0013;
        spec.initial_y = 0x0003;
        spec.wrapper_pc = 0x001B4C4E;
        break;
    case 0x05:
        spec.loader_pc = 0x001B49DA;
        spec.c000_source = 0x0012D0FA;
        spec.palette_band0 = 0x00129952;
        spec.palette_band1 = 0x00129A92;
        if (secondary) {
            spec.command_stream = 0x001275EE;
            spec.initial_x = 0x0011;
            spec.initial_y = 0x0007;
            spec.wrapper_pc = 0x001B4C9A;
        } else {
            spec.command_stream = 0x00127571;
            spec.initial_x = 0x0012;
            spec.initial_y = 0x0008;
            spec.wrapper_pc = 0x001B4C74;
        }
        break;
    case 0x07:
        spec.loader_pc = 0x001B498A;
        spec.c000_source = 0x0012D870;
        spec.palette_band0 = 0x00129912;
        spec.palette_band1 = 0x00129AB2;
        spec.command_stream = 0x0012772D;
        spec.initial_x = 0x0015;
        spec.initial_y = 0x000A;
        spec.wrapper_pc = 0x001B4CC0;
        break;
    case 0x0B:
        spec.loader_pc = 0x001B4A02;
        spec.c000_source = 0x0012DF6C;
        spec.palette_band0 = 0x00129972;
        spec.palette_band1 = 0x00129AB2;
        spec.command_stream = 0x0012792B;
        spec.initial_x = 0x0012;
        spec.initial_y = 0x0007;
        spec.wrapper_pc = 0x001B4DD2;
        break;
    default:
        return result;
    }
    spec.supported = true;

    write32(core.ram, kSceneResourceC000Source, spec.c000_source);
    write32(core.ram, kSceneResourcePaletteSource, spec.palette_band0);
    if (effects.load_or_clear_c000 != nullptr) {
        effects.load_or_clear_c000(effects.context, spec.c000_source);
    }
    if (effects.prepare_scene_palette != nullptr) {
        effects.prepare_scene_palette(
            effects.context, spec.palette_band0, spec.palette_band1);
    }
    if (trace != nullptr) {
        trace->scene_resource_presentation_selected = true;
        trace->scene_resource_presentation_state = scene_state;
        trace->scene_resource_presentation_secondary = secondary;
        trace->scene_resource_presentation_loader = spec.loader_pc;
        trace->scene_resource_presentation_wrapper = spec.wrapper_pc;
        trace->scene_resource_presentation_c000_source = spec.c000_source;
        trace->scene_resource_presentation_palette_band0 = spec.palette_band0;
        trace->scene_resource_presentation_palette_band1 = spec.palette_band1;
        trace->scene_resource_presentation_stream = spec.command_stream;
        trace->scene_resource_presentation_initial_x = spec.initial_x;
        trace->scene_resource_presentation_initial_y = spec.initial_y;
    }
    result.stream = scene_resource_process_command_stream_with_presentation_scratch(
        core, spec.command_stream, spec.initial_x, spec.initial_y,
        effects, trace, instruction_budget);
    return result;
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
