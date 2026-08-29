#pragma once

#include "game_ram.hpp"
#include "game_state.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace openaladdin {

// Semantic result of one SceneResource_ProcessCommandStream invocation. The
// original interpreter runs until command 0 returns or SCENE_RESOURCE_STATUS
// becomes nonzero; the budget exists only to keep malformed streams bounded in
// native tooling.
enum class SceneResourceRunResult {
    Finished,
    StatusChanged,
    BudgetExhausted,
    InvalidStream,
};

struct SceneTileWrite {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint8_t tile_row = 0;
    std::uint16_t tile_base = 0;
};

struct SceneActorRecord {
    std::uint32_t template_address = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
};

// SceneResourceVm interprets the compact ROM language, while these callbacks
// express its VDP, frame, palette, and actor effects in native semantic terms.
// No SDL or fake VDP port is needed at this boundary.
struct SceneServices {
    using TileWriter = std::function<void(GameState&, const SceneTileWrite&)>;
    using ServiceFrame = std::function<void(GameState&)>;
    using ResourceLoader = std::function<void(GameState&)>;
    using PalettePreparer = std::function<void(GameState&)>;
    using ActorInstantiator = std::function<bool(GameState&, const SceneActorRecord&)>;

    TileWriter write_tile;
    ServiceFrame service_frame;
    ResourceLoader load_or_clear_c000;
    PalettePreparer prepare_frame_and_palette;
    ActorInstantiator instantiate_actor;
};

// The original 68000 interpreter has no instruction-count limit. Native
// callers get a generous default guard so an invalid pointer cannot hang a
// test or a headless tool forever.
class SceneResourceVm {
public:
    static constexpr std::size_t kDefaultInstructionBudget = 1'000'000;

    void bind_rom(const std::vector<std::uint8_t>& rom) { rom_ = &rom; }
    void reset();
    void start(RamAddress stream);
    SceneResourceRunResult tick(
        GameState& state,
        SceneServices& services,
        std::size_t instruction_budget = kDefaultInstructionBudget
    );
    SceneResourceRunResult tick_with_presentation_scratch(
        GameState& state,
        SceneServices& services,
        std::size_t instruction_budget = kDefaultInstructionBudget
    );

    bool started() const { return started_; }
    bool finished() const { return finished_; }
    bool faulted() const { return faulted_; }
    RamAddress cursor() const { return cursor_; }
    RamAddress stream_pointer() const { return stream_pointer_; }
    std::uint16_t tile_x() const { return tile_x_; }
    std::uint16_t tile_y() const { return tile_y_; }
    std::uint16_t tile_base() const { return tile_base_; }
    bool presentation_scratch() const { return presentation_scratch_; }

private:
    enum class StepResult {
        Continue,
        Finished,
        StatusChanged,
        InvalidStream,
    };

    bool can_read(std::size_t count) const;
    std::uint8_t read8();
    std::uint16_t read16();
    std::uint32_t read32();
    std::uint32_t read24();
    static std::uint16_t increment_low_byte(std::uint16_t value);
    StepResult dispatch_command(std::uint8_t command, GameState& state, SceneServices& services);
    SceneResourceRunResult run(
        GameState& state,
        SceneServices& services,
        std::size_t instruction_budget,
        bool presentation_scratch
    );
    StepResult write_tile_run(
        GameState& state,
        SceneServices& services,
        std::uint16_t width,
        std::uint16_t height,
        bool advance_y,
        bool restore_x_each_row
    );

    const std::vector<std::uint8_t>* rom_ = nullptr;
    RamAddress cursor_ = 0;
    RamAddress stream_pointer_ = 0;
    std::uint16_t tile_x_ = 0;
    std::uint16_t tile_y_ = 0;
    std::uint16_t tile_base_ = 0;
    bool started_ = false;
    bool finished_ = false;
    bool faulted_ = false;
    bool presentation_scratch_ = false;
};

}  // namespace openaladdin
