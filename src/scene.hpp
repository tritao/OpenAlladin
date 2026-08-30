#pragma once

#include "level.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace openaladdin {

struct SceneInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
};

struct SceneRuntimeState {
    int state = 1;
    std::uint32_t script_cursor = 0;
    std::uint8_t script_countdown = 0;
    // SCENE_SCRIPT_PENDING (FFF57C) is kept even when the native slice has
    // no loaded script payload. It provides the recovered gates for the
    // ordinal-29 advance, ordinal-35 completion check, and ordinal-37 write.
    std::uint8_t script_pending = 0;
    // DAT_00FFF140 is the status polled by the ordinal-35 completion gate.
    std::uint8_t resource_status = 0;
    // SCENE_RESOURCE_COMPLETION and SCENE_RESOURCE_MODE are the separate
    // publication fields used by collision/resource transition handlers.
    std::uint8_t resource_completion = 0; // FFF114
    std::uint8_t resource_mode = 0;       // FFF15A
    std::uint8_t transition_event = 0;
    bool transition_active = false;
};

// Owns scene-state selection and the transition-only player branch. Level
// loading remains in Level, while this class supplies the immutable descriptor
// selected by SCENE_STATE and the small runtime state needed by transitions.
class SceneSystem {
public:
    void load_rom(const std::string& rom_path);
    void load_rom_bytes(const std::vector<std::uint8_t>& rom);
    // Engine binds this service to GameState so scene runtime fields have one
    // owner. Standalone SceneSystem users retain the internal fallback state.
    void bind_runtime(SceneRuntimeState& runtime) { runtime_override_ = &runtime; }

    void reset(int scene_state = 1);
    void select(int scene_state);

    int scene_state() const { return runtime_state().state; }
    bool is_transition() const { return runtime_state().transition_active; }
    const SceneRuntimeState& runtime() const { return runtime_state(); }
    const LevelDescriptor* descriptor(int scene_state) const;
    const LevelDescriptor* current_descriptor() const { return descriptor(runtime_state().state); }
    void write_checkpoint(std::ostream& output) const;
    void read_checkpoint(std::istream& input, bool extended_state = true);

    // Returns true when this scene owns the update. The transition scene uses
    // eight-pixel local-coordinate movement and bypasses normal physics.
    bool update_transition(
        const SceneInput& input,
        int& player_x,
        int& player_y,
        bool& grounded
    );

    // These are the scene-script gate services called unconditionally by
    // Game_FrameUpdateLoop. Resource payload execution is owned by the
    // Engine-bound SceneResourceVm; these methods retain only the scene-state
    // gates and transition behavior.
    bool advance_script();
    bool service_level_exit(
        int player_world_y,
        int level_height,
        std::uint8_t& terminal_transition,
        std::uint8_t& interaction_lock
    );
    bool transition_completion_ready() const;
    bool complete_script_to_state1();

private:
    SceneRuntimeState& runtime_state() { return runtime_override_ != nullptr ? *runtime_override_ : runtime_; }
    const SceneRuntimeState& runtime_state() const { return runtime_override_ != nullptr ? *runtime_override_ : runtime_; }

    std::vector<LevelDescriptor> descriptors_;
    SceneRuntimeState runtime_{};
    SceneRuntimeState* runtime_override_ = nullptr;
};

}  // namespace openaladdin
