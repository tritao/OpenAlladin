#include "scene.hpp"

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

}  // namespace openaladdin
