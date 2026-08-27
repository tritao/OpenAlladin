#include "scene.hpp"

#include "checkpoint_io.hpp"

#include <fstream>
#include <stdexcept>

namespace openaladdin {
namespace {

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open " + path);
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) throw std::runtime_error("cannot size " + path);
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!file) throw std::runtime_error("cannot read " + path);
    return data;
}

}  // namespace

void SceneSystem::load_rom(const std::string& rom_path) {
    load_rom_bytes(read_file(rom_path));
}

void SceneSystem::load_rom_bytes(const std::vector<std::uint8_t>& rom) {
    descriptors_ = rom.empty() ? std::vector<LevelDescriptor>{}
                                : LevelTable::descriptors(rom);
}

void SceneSystem::reset(int scene_state) {
    runtime_ = {};
    runtime_.state = scene_state;
    runtime_.transition_active = scene_state == 8;
}

void SceneSystem::select(int scene_state) {
    runtime_.state = scene_state;
    runtime_.transition_active = scene_state == 8;
}

const LevelDescriptor* SceneSystem::descriptor(int scene_state) const {
    if (scene_state < 0 || scene_state >= static_cast<int>(descriptors_.size())) {
        return nullptr;
    }
    return &descriptors_[static_cast<std::size_t>(scene_state)];
}

bool SceneSystem::update_transition(
    const SceneInput& input,
    int& player_x,
    int& player_y,
    bool& grounded
) {
    if (!runtime_.transition_active) return false;
    if (input.right && player_x < 0x130) player_x += 8;
    if (input.left && player_x >= 0x10) player_x -= 8;
    if (input.up && player_y >= 0x10) player_y -= 8;
    if (input.down && player_y < 0x1E0) player_y += 8;
    grounded = false;
    return true;
}

void SceneSystem::write_checkpoint(std::ostream& output) const {
    checkpoint::Writer writer(output);
    writer.i32(runtime_.state);
    writer.u32(runtime_.script_cursor);
    writer.u8(runtime_.script_countdown);
    writer.u8(runtime_.script_pending);
    writer.u8(runtime_.resource_status);
    writer.u8(runtime_.transition_event);
    writer.boolean(runtime_.transition_active);
}

void SceneSystem::read_checkpoint(std::istream& input) {
    checkpoint::Reader reader(input);
    SceneRuntimeState runtime;
    runtime.state = reader.i32();
    runtime.script_cursor = reader.u32();
    runtime.script_countdown = reader.u8();
    runtime.script_pending = reader.u8();
    runtime.resource_status = reader.u8();
    runtime.transition_event = reader.u8();
    runtime.transition_active = reader.boolean();
    if (!descriptors_.empty()
        && (runtime.state < 0 || runtime.state >= static_cast<int>(descriptors_.size()))) {
        throw std::runtime_error("scene state is outside the loaded OpenAladdin ROM");
    }
    if (runtime.transition_active != (runtime.state == 8)) {
        throw std::runtime_error("invalid scene transition state in OpenAladdin checkpoint");
    }
    runtime_ = runtime;
}

bool SceneSystem::advance_script() {
    if (runtime_.script_countdown == 0) return false;
    if (runtime_.script_countdown != 0xFF) {
        --runtime_.script_countdown;
        if (runtime_.script_countdown != 0) return false;
    }
    // The native loader has no script payload/cursor table to consume. The
    // countdown gate is still serviced, and the caller reaches the next
    // ordinal even when the payload branch has no state mutation to apply.
    return true;
}

bool SceneSystem::service_level_exit(
    int player_world_y,
    int level_height,
    std::uint8_t& terminal_transition,
    std::uint8_t& interaction_lock
) {
    // FUN_001A8F0C is always called. The ROM first takes the level-exit path
    // only when PLAYER_WORLD_Y is beyond LEVEL_HEIGHT_PIXELS + 0x100. The
    // native scene slice has no destination/resource table to rebuild, so
    // retain that branch as a handled no-op.
    if (player_world_y > level_height + 0x100) {
        return true;
    }
    if (terminal_transition == 0) return false;
    if (terminal_transition != 0xFF && terminal_transition != 1) {
        ++terminal_transition;
        return false;
    }
    terminal_transition = 0;
    if (interaction_lock != 0) return false;
    interaction_lock = 0;
    // Scene state 8 increments an internal transition-attempt counter in the
    // ROM. That counter has no native destination table or observable owner;
    // the state/transition gates above are the complete effect in this slice.
    return true;
}

bool SceneSystem::transition_completion_ready() const {
    // FUN_001AE0F6 waits only while pending != 2 and DAT_00FFF140 is nonzero.
    return runtime_.script_pending == 2 || runtime_.resource_status == 0;
}

bool SceneSystem::complete_script_to_state1() {
    if (runtime_.script_pending != 1 || runtime_.script_cursor == 0) return false;
    runtime_.script_pending = 0;
    runtime_.state = 1;
    runtime_.transition_active = false;
    return true;
}

}  // namespace openaladdin
