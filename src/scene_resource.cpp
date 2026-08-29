#include "scene_resource.hpp"

namespace openaladdin {

void SceneResourceVm::reset() {
    cursor_ = 0;
    stream_pointer_ = 0;
    tile_x_ = 0;
    tile_y_ = 0;
    tile_base_ = 0;
    started_ = false;
    finished_ = false;
    faulted_ = false;
    presentation_scratch_ = false;
}

void SceneResourceVm::start(RamAddress stream) {
    reset();
    cursor_ = stream;
    started_ = true;
}

bool SceneResourceVm::can_read(std::size_t count) const {
    if (rom_ == nullptr) return false;
    const auto offset = static_cast<std::size_t>(cursor_);
    return offset <= rom_->size() && count <= rom_->size() - offset;
}

std::uint8_t SceneResourceVm::read8() {
    return (*rom_)[static_cast<std::size_t>(cursor_++)];
}

std::uint16_t SceneResourceVm::read16() {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(read8()) << 8) | read8());
}

std::uint32_t SceneResourceVm::read32() {
    return (static_cast<std::uint32_t>(read8()) << 24)
        | (static_cast<std::uint32_t>(read8()) << 16)
        | (static_cast<std::uint32_t>(read8()) << 8)
        | read8();
}

std::uint32_t SceneResourceVm::read24() {
    return (static_cast<std::uint32_t>(read8()) << 16)
        | (static_cast<std::uint32_t>(read8()) << 8)
        | read8();
}

std::uint16_t SceneResourceVm::increment_low_byte(std::uint16_t value) {
    return static_cast<std::uint16_t>(
        (value & 0xFF00U) | static_cast<std::uint8_t>(value + 1));
}

SceneResourceVm::StepResult SceneResourceVm::write_tile_run(
    GameState& state,
    SceneServices& services,
    std::uint16_t width,
    std::uint16_t height,
    bool advance_y,
    bool restore_x_each_row
) {
    if (!can_read(1)) return StepResult::InvalidStream;
    const std::uint8_t tile_row = static_cast<std::uint8_t>(read8() - 0x20U);
    for (std::uint16_t row = 0; row < height; ++row) {
        const std::uint16_t row_x = tile_x_;
        for (std::uint16_t column = 0; column < width; ++column) {
            if (services.write_tile) {
                services.write_tile(state, SceneTileWrite{
                    tile_x_, tile_y_, tile_row, tile_base_});
            }
            tile_x_ = increment_low_byte(tile_x_);
        }
        if (restore_x_each_row) tile_x_ = row_x;
        if (advance_y) tile_y_ = increment_low_byte(tile_y_);
    }
    return StepResult::Continue;
}

SceneResourceVm::StepResult SceneResourceVm::dispatch_command(
    std::uint8_t command,
    GameState& state,
    SceneServices& services
) {
    switch (command) {
    case 0x00:
        finished_ = true;
        return StepResult::Finished;

    case 0x01: {
        if (!can_read(1)) return StepResult::InvalidStream;
        const auto delta = static_cast<std::int8_t>(read8());
        tile_x_ = static_cast<std::uint16_t>(tile_x_ + delta);
        return StepResult::Continue;
    }

    case 0x02: {
        if (!can_read(1)) return StepResult::InvalidStream;
        const auto delta = static_cast<std::int8_t>(read8());
        tile_y_ = static_cast<std::uint16_t>(tile_y_ + delta);
        return StepResult::Continue;
    }

    case 0x03: {
        if (!can_read(2)) return StepResult::InvalidStream;
        const auto count_byte = read8();
        const std::uint16_t count = count_byte == 0 ? 0x100 : count_byte;
        return write_tile_run(state, services, count, 1, false, false);
    }

    case 0x04: {
        if (!can_read(2)) return StepResult::InvalidStream;
        const auto count_byte = read8();
        const std::uint16_t count = count_byte == 0 ? 0x100 : count_byte;
        return write_tile_run(state, services, 1, count, true, true);
    }

    case 0x05: {
        if (!can_read(3)) return StepResult::InvalidStream;
        const std::uint8_t width_byte = read8();
        const std::uint8_t height_byte = read8();
        const std::uint16_t width = width_byte == 0 ? 0x100 : width_byte;
        const std::uint16_t height = height_byte == 0 ? 0x100 : height_byte;
        return write_tile_run(state, services, width, height, true, true);
    }

    case 0x06: {
        if (!can_read(1)) return StepResult::InvalidStream;
        const std::uint8_t frame_byte = read8();
        const std::uint16_t frames = frame_byte == 0 ? 0x100 : frame_byte;
        for (std::uint16_t frame = 0; frame < frames; ++frame) {
            if (services.service_frame) services.service_frame(state);
            if (state.scene.resource_status != 0) return StepResult::StatusChanged;
        }
        return StepResult::Continue;
    }

    case 0x07:
        tile_y_ = increment_low_byte(tile_y_);
        tile_x_ = static_cast<std::uint16_t>(tile_x_ & 0xFF00U);
        return StepResult::Continue;

    case 0x08:
        tile_base_ = 0x0000;
        return StepResult::Continue;
    case 0x09:
        tile_base_ = 0x2000;
        return StepResult::Continue;
    case 0x0A:
        tile_base_ = 0x4000;
        return StepResult::Continue;
    case 0x0B:
        tile_base_ = 0x6000;
        return StepResult::Continue;

    case 0x0C:
        if (!can_read(3)) return StepResult::InvalidStream;
        stream_pointer_ = read24();
        return StepResult::Continue;

    case 0x0D:
        if (services.load_or_clear_c000) services.load_or_clear_c000(state);
        return StepResult::Continue;

    case 0x0E:
        if (services.prepare_frame_and_palette) services.prepare_frame_and_palette(state);
        return StepResult::Continue;

    case 0x0F: {
        if (!can_read(8)) return StepResult::InvalidStream;
        const SceneActorRecord record{read32(), read16(), read16()};
        if (services.instantiate_actor && !services.instantiate_actor(state, record)) {
            // Actor_FindFreeSlot simply skips the record when its pool is
            // exhausted; the stream itself remains valid and continues.
        }
        return StepResult::Continue;
    }

    default:
        // Commands 0x10..0x1F are present in the ROM table and are allowed to
        // be added as their handler contracts are recovered. Keeping them as
        // no-ops would hide a stream mistake, so report the unsupported byte.
        return StepResult::InvalidStream;
    }
}

SceneResourceRunResult SceneResourceVm::run(
    GameState& state,
    SceneServices& services,
    std::size_t instruction_budget,
    bool presentation_scratch
) {
    presentation_scratch_ = presentation_scratch;
    if (!started_ || finished_) return finished_
        ? SceneResourceRunResult::Finished
        : SceneResourceRunResult::InvalidStream;
    if (state.scene.resource_status != 0) return SceneResourceRunResult::StatusChanged;

    for (std::size_t instruction = 0; instruction < instruction_budget; ++instruction) {
        // The ROM tests SCENE_RESOURCE_STATUS at the top of every interpreter
        // iteration, including after handlers that perform native services.
        if (state.scene.resource_status != 0) {
            return SceneResourceRunResult::StatusChanged;
        }
        if (!can_read(1)) {
            faulted_ = true;
            return SceneResourceRunResult::InvalidStream;
        }
        const std::uint8_t raw = read8();
        StepResult result;
        if (raw < 0x20) {
            result = dispatch_command(raw, state, services);
        } else {
            if (services.write_tile) {
                services.write_tile(state, SceneTileWrite{
                    tile_x_, tile_y_, static_cast<std::uint8_t>(raw - 0x20U), tile_base_});
            }
            tile_x_ = increment_low_byte(tile_x_);
            result = StepResult::Continue;
        }

        switch (result) {
        case StepResult::Continue:
            continue;
        case StepResult::Finished:
            return SceneResourceRunResult::Finished;
        case StepResult::StatusChanged:
            return SceneResourceRunResult::StatusChanged;
        case StepResult::InvalidStream:
            faulted_ = true;
            return SceneResourceRunResult::InvalidStream;
        }
    }
    return SceneResourceRunResult::BudgetExhausted;
}

SceneResourceRunResult SceneResourceVm::tick(
    GameState& state,
    SceneServices& services,
    std::size_t instruction_budget
) {
    const auto result = run(state, services, instruction_budget, false);
    presentation_scratch_ = false;
    return result;
}

SceneResourceRunResult SceneResourceVm::tick_with_presentation_scratch(
    GameState& state,
    SceneServices& services,
    std::size_t instruction_budget
) {
    const auto result = run(state, services, instruction_budget, true);
    presentation_scratch_ = false;
    return result;
}

}  // namespace openaladdin
