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
    writer.u8(runtime_.transition_event);
    writer.boolean(runtime_.transition_active);
}

void SceneSystem::read_checkpoint(std::istream& input) {
    checkpoint::Reader reader(input);
    SceneRuntimeState runtime;
    runtime.state = reader.i32();
    runtime.script_cursor = reader.u32();
    runtime.script_countdown = reader.u8();
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

}  // namespace openaladdin
