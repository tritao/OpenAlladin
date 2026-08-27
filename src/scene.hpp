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

    void reset(int scene_state = 1);
    void select(int scene_state);

    int scene_state() const { return runtime_.state; }
    bool is_transition() const { return runtime_.transition_active; }
    const SceneRuntimeState& runtime() const { return runtime_; }
    const LevelDescriptor* descriptor(int scene_state) const;
    const LevelDescriptor* current_descriptor() const { return descriptor(runtime_.state); }
    void write_checkpoint(std::ostream& output) const;
    void read_checkpoint(std::istream& input);

    // Returns true when this scene owns the update. The transition scene uses
    // eight-pixel local-coordinate movement and bypasses normal physics.
    bool update_transition(
        const SceneInput& input,
        int& player_x,
        int& player_y,
        bool& grounded
    );

private:
    std::vector<LevelDescriptor> descriptors_;
    SceneRuntimeState runtime_{};
};

}  // namespace openaladdin
