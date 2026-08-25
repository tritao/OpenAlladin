#include "engine.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace openaladdin {
namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 224;
constexpr int kTerrainVisualOffsetY = 0xF0;
constexpr std::uint32_t kTerrainNoOpHandler = 0x001B65BE;

// Camera_UpdateFollow (0x001AA90C) indexes these fixed-ROM byte tables by
// the absolute local-coordinate error. The horizontal table occupies ROM
// 0x002A52..0x002B51. The vertical table ends at the level table at 0x002C78;
// the original camera never presents an error outside this 0xD4-byte range.
constexpr std::array<std::uint8_t, 0x100> kCameraHorizontalDampening = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x01, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x03, 0x03, 0x04, 0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x04, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    0x06, 0x07, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x08, 0x07, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x09, 0x08, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x09, 0x09, 0x09, 0x09, 0x0A, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0A, 0x0B, 0x0A, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
    0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0C, 0x0B, 0x0C, 0x0C, 0x0C,
    0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0E, 0x0D, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E
};

constexpr std::array<std::uint8_t, 0xD4> kCameraVerticalDampening = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07,
    0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
    0x0B, 0x0B, 0x0B, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
    0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
    0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0F,
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
    0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
    0x0F, 0x0F, 0x0F, 0x00
};
// the native slice makes the behavior -> handler decision data-driven and
// avoids treating an arbitrary behavior-byte range as solid terrain.
constexpr std::array<std::uint32_t, 0x48> kTerrainHandlers = {
    0x001B65BE, 0x001B5492, 0x001B5492, 0x001B5492,
    0x001B5492, 0x001B549C, 0x001B549C, 0x001B549C,
    0x001B65BE, 0x001B54F4, 0x001B5320, 0x001B54F4,
    0x001B54F4, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B5450, 0x001B65BE, 0x001B56F4, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B5458, 0x001B5460,
    0x001B5468, 0x001B65BE, 0x001B65BE, 0x001B575C,
    0x001B5764, 0x001B576C, 0x001B65BE, 0x001B5774,
    0x001B5318, 0x001B65BE, 0x001B54D8, 0x001B54D8,
    0x001B54D2, 0x001B54E0, 0x001B65BE, 0x001B54A6,
    0x001B55E8, 0x001B557E, 0x001B55D8, 0x001B5502,
    0x001B65BE, 0x001B56B6, 0x001B65BE, 0x001B65BE,
    0x001B537A, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B65BE,
    0x001B536C, 0x001B53A2, 0x001B65BE, 0x001B65BE,
    0x001B65BE, 0x001B65BE, 0x001B65BE, 0x001B5470
};

std::uint32_t terrain_handler(std::uint8_t behavior) {
    return behavior < kTerrainHandlers.size() ? kTerrainHandlers[behavior] : kTerrainNoOpHandler;
}

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + path);
    }
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) {
        throw std::runtime_error("cannot size " + path);
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!file) {
        throw std::runtime_error("cannot read " + path);
    }
    return data;
}

std::string ppm_token(std::istream& input) {
    while (true) {
        int ch = input.peek();
        if (ch == '#') {
            std::string ignored;
            std::getline(input, ignored);
        } else if (ch != EOF && std::isspace(static_cast<unsigned char>(ch))) {
            input.get();
        } else {
            break;
        }
    }
    std::string token;
    while (true) {
        int ch = input.peek();
        if (ch == EOF || std::isspace(static_cast<unsigned char>(ch)) || ch == '#') {
            break;
        }
        token.push_back(static_cast<char>(input.get()));
    }
    return token;
}

struct PpmImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

PpmImage read_ppm(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + path);
    }
    if (ppm_token(file) != "P6") {
        throw std::runtime_error(path + " is not a binary PPM (P6)");
    }
    const int width = std::stoi(ppm_token(file));
    const int height = std::stoi(ppm_token(file));
    const int max_value = std::stoi(ppm_token(file));
    if (width <= 0 || height <= 0 || max_value != 255) {
        throw std::runtime_error("unsupported PPM dimensions or color depth in " + path);
    }
    // ppm_token leaves the delimiter after max_value in the stream. Consume
    // it before reading the packed RGB payload.
    file.get();
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * height * 3);
    file.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (file.gcount() != static_cast<std::streamsize>(rgb.size())) {
        throw std::runtime_error("truncated PPM payload in " + path);
    }
    PpmImage result{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
    for (std::size_t source = 0, destination = 0; source < rgb.size(); source += 3, destination += 4) {
        result.rgba[destination + 0] = rgb[source + 0];
        result.rgba[destination + 1] = rgb[source + 1];
        result.rgba[destination + 2] = rgb[source + 2];
        result.rgba[destination + 3] = 255;
    }
    return result;
}

std::vector<std::uint16_t> read_be_words(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() % 2 != 0) {
        throw std::runtime_error("terrain map has an odd byte count");
    }
    std::vector<std::uint16_t> words(bytes.size() / 2);
    for (std::size_t i = 0; i < words.size(); ++i) {
        words[i] = static_cast<std::uint16_t>(bytes[i * 2] << 8 | bytes[i * 2 + 1]);
    }
    return words;
}

std::uint8_t fixed_high_byte(std::int16_t value) {
    return static_cast<std::uint8_t>(static_cast<std::uint16_t>(value) >> 8);
}

std::uint32_t rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    return static_cast<std::uint32_t>(r) |
           (static_cast<std::uint32_t>(g) << 8) |
           (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(a) << 24);
}

}  // namespace

void Level::load(const std::string& asset_root) {
    const auto background = read_ppm(asset_root + "/background.ppm");
    background_width_ = background.width;
    background_height_ = background.height;
    background_rgba_ = background.rgba;

    const auto map_bytes = read_file(asset_root + "/raw/map.bin");
    terrain_words_ = read_be_words(map_bytes);
    if (terrain_words_.size() != static_cast<std::size_t>(map_width_ * map_height_)) {
        throw std::runtime_error("level-01 map is not 300x45 terrain words");
    }

    floor_data_ = read_file(asset_root + "/raw/floor.bin");
    const auto palette_bytes = read_file(asset_root + "/raw/palette.bin");
    if (palette_bytes.size() < 32) {
        throw std::runtime_error("level palette is too short");
    }
    palette_.clear();
    palette_.reserve(palette_bytes.size() / 2);
    for (std::size_t i = 0; i + 1 < palette_bytes.size(); i += 2) {
        const std::uint16_t word = static_cast<std::uint16_t>(palette_bytes[i] << 8 | palette_bytes[i + 1]);
        const auto channel = [](std::uint16_t value, int shift) {
            return static_cast<std::uint8_t>(((value >> shift) & 7) * 255 / 7);
        };
        palette_.push_back(SDL_Color{channel(word, 1), channel(word, 5), channel(word, 9), 255});
    }
}

std::uint8_t Level::terrain_behavior(int column, int row) const {
    if (column < 0 || column >= map_width_ || row < 0 || row >= map_height_) {
        return 0xFF;
    }
    const std::uint16_t terrain_word = terrain_words_[static_cast<std::size_t>(row * map_width_ + column)];
    const std::size_t behavior_index = 2 + (terrain_word >> 1);
    if (behavior_index >= floor_data_.size()) {
        return 0xFF;
    }
    return floor_data_[behavior_index];
}

Level::TerrainCell Level::resolve_player_cell(int world_x, int world_y) const {
    TerrainCell cell;
    const int terrain_y = world_y - kTerrainVisualOffsetY;
    if (terrain_y < 0 || terrain_y >= map_height_ * 16) {
        return cell;
    }

    const int row = terrain_y >> 4;
    const int column = (world_x + 16) >> 4;
    if (column < 0 || column >= map_width_ || row < 0 || row >= map_height_) {
        return cell;
    }

    const std::uint16_t word = terrain_words_[static_cast<std::size_t>(row * map_width_ + column)];
    const std::size_t behavior_index = 2 + (word >> 1);
    if (behavior_index >= floor_data_.size()) {
        return cell;
    }

    cell.valid = true;
    cell.column = column;
    cell.row = row;
    cell.terrain_word = word;
    cell.behavior = floor_data_[behavior_index];
    cell.handler = terrain_handler(cell.behavior);
    return cell;
}

Level::TerrainQuery Level::query_player(int world_x, int world_y) const {
    TerrainQuery query;
    query.resolver = resolve_player_cell(world_x, world_y);

    // These are the four single-cell edge probes used by the native response
    // path. They deliberately do not search neighboring rows or scan a
    // player-sized rectangle. The resolver's own probe remains the canonical
    // floor/contour lookup above.
    query.left = resolve_player_cell(world_x - 8, world_y - 8);
    query.right = resolve_player_cell(world_x + 8, world_y - 8);
    query.up = resolve_player_cell(world_x, world_y - 24);
    query.down = resolve_player_cell(world_x, world_y + 8);
    return query;
}

void Engine::load(
    const std::string& asset_root,
    const std::string& sprite_root,
    const std::string& rom_path
) {
    level_.load(asset_root);
    sprites_.load(sprite_root.empty() ? asset_root + "/../../sprites" : sprite_root);
    // Level-01 captures show the player using CRAM line 2. Reuse the exact
    // extracted scene palette instead of the Chopper preview fallback.
    sprites_.set_palette(level_.palette());
    if (!rom_path.empty()) {
        animation_.load_rom(rom_path);
    }
    framebuffer_.resize(static_cast<std::size_t>(kScreenWidth * kScreenHeight));
    reset();
}

void Engine::reset() {
    player_ = PlayerState{};
    camera_ = CameraState{};
    player_.x = level_.start_x();
    player_.y = level_.start_y();
    camera_.x = level_.camera_start_x();
    camera_.y = level_.camera_start_y();
    camera_.reference_x = camera_.x;
    camera_.reference_y = camera_.y;
    camera_.horizontal_threshold = level_.camera_threshold_x();
    camera_.vertical_threshold = level_.camera_threshold_y();
    camera_.level_width = level_.map_width() * 16;
    camera_.level_height = level_.map_height() * 16;
    camera_.scene_state = 1;
    player_.grounded = true;
    initialize_camera_alignment();
    const auto initial_cell = level_.resolve_player_cell(player_world_x(), player_world_y());
    player_.terrain_behavior = initial_cell.valid ? initial_cell.behavior : 0;
    player_.terrain_landing_state = player_.terrain_behavior != 0 ? 1 : 0;
    frame_ = 0;
    last_ground_direction_ = 0;
    quit_ = false;
    animation_.reset();
}

void Engine::set_checkpoint(int x, int y, std::int16_t vx, std::int16_t vy, bool grounded) {
    player_.x = x;
    player_.y = y;
    player_.vx = vx;
    player_.vy = vy;
    player_.grounded = grounded;
    const auto checkpoint_cell = level_.resolve_player_cell(player_world_x(), player_world_y());
    player_.terrain_behavior = checkpoint_cell.valid ? checkpoint_cell.behavior : 0;
    player_.terrain_landing_state = grounded && player_.terrain_behavior != 0 ? 1 : 0;
    frame_ = 0;
    quit_ = false;
    animation_.reset();
    if (!grounded) {
        animation_.update(
            SpritePose::Jump,
            false,
            AnimationContext{
                player_.x,
                player_.y,
                player_world_x(),
                player_world_y(),
                player_.vx,
                player_.vy,
                grounded,
                player_.terrain_response_timer_state,
            }
        );
    }
}

void Engine::set_checkpoint_frame_ptr(int address) {
    if (animation_.rom_loaded()) {
        animation_.set_frame_pointer(static_cast<std::uint32_t>(address));
        return;
    }
    const int frame = sprites_.frame_index_for_address(address);
    if (frame < 0 || !animation_.set_frame(frame)) {
        animation_.reset();
    }
}

void Engine::set_checkpoint_animation(std::uint32_t animation_pc, int timer) {
    animation_.set_animation_state(animation_pc, timer);
}

void Engine::set_checkpoint_camera(int x, int y, int reference_x, int reference_y, int scroll_x, int scroll_y, int scene_state) {
    camera_.x = x;
    camera_.y = y;
    camera_.reference_x = reference_x;
    camera_.reference_y = reference_y;
    camera_.scroll_x = scroll_x;
    camera_.scroll_y = scroll_y;
    camera_.scene_state = scene_state;
    camera_.special_mode = scene_state == 8 ? 1 : 0;
}

int Engine::visual_x() const {
    return player_world_x();
}

int Engine::visual_y() const {
    // The terrain resolver indexes rows from WORLD_CAMERA_Y + PLAYER_Y - 0xF0.
    return player_world_y() - kTerrainVisualOffsetY;
}

SpritePose Engine::sprite_pose() const {
    return animation_.pose();
}

void Engine::update_terrain_input(const InputState& input) {
    // FFF156 is the active-low controller/query byte consumed by the four
    // Terrain_TestQueryFlag helpers. The main loop turns direction bits into
    // FFF07C..FFF07F with SEQ, so a pressed direction is represented by 0xFF.
    player_.terrain_query_result = 0x7F;
    if (input.up) player_.terrain_query_result &= static_cast<std::uint8_t>(~0x01);
    if (input.down) player_.terrain_query_result &= static_cast<std::uint8_t>(~0x02);
    if (input.left) player_.terrain_query_result &= static_cast<std::uint8_t>(~0x04);
    if (input.right) player_.terrain_query_result &= static_cast<std::uint8_t>(~0x08);
    if (input.jump_pressed) player_.terrain_query_result &= static_cast<std::uint8_t>(~0x20);

    player_.terrain_push_right = input.right ? 0xFF : 0;
    player_.terrain_push_left = input.left ? 0xFF : 0;
    player_.terrain_push_up = input.up ? 0xFF : 0;
    player_.terrain_push_down = input.down ? 0xFF : 0;
}

void Engine::integrate_motion() {
    // This follows Player_IntegrateMotion at 0x001A9B90: move by the signed
    // high byte of each 8.8 velocity, then damp by 0x28/0x3C.
    if (player_.vx != 0) {
        if (player_.vx < 0) {
            if (player_.terrain_stop_left_motion != 0
                || player_.x < 0x14
                || static_cast<std::uint16_t>(-player_.vx) < 0x28) {
                player_.vx = 0;
            } else {
                player_.x += static_cast<std::int8_t>(fixed_high_byte(player_.vx));
                player_.vx = static_cast<std::int16_t>(player_.vx + 0x28);
            }
        } else if (player_.vx < 0x28 || player_.terrain_stop_right_motion != 0) {
            player_.vx = 0;
        } else {
            if (player_.x < 0x130) {
                player_.x += static_cast<std::int8_t>(fixed_high_byte(player_.vx));
            }
            player_.vx = static_cast<std::int16_t>(player_.vx - 0x28);
        }
    }

    if (player_.vy == 0) {
        return;
    }
    if (player_.vy < 0) {
        const std::int16_t magnitude = static_cast<std::int16_t>(-player_.vy);
        if (player_.y < 0x14) {
            player_.vy = static_cast<std::int16_t>(player_.vy + 0x3C);
        } else if (player_.terrain_stop_upward_motion != 0) {
            player_.terrain_response_active = 0;
            player_.terrain_vertical_stop = 0xFF;
            player_.terrain_response_latch = 0;
            player_.vy = 0;
        } else if (magnitude > 0x3B) {
            player_.y += static_cast<std::int8_t>(fixed_high_byte(player_.vy));
            player_.vy = static_cast<std::int16_t>(player_.vy + 0x3C);
        } else {
            // 0x001A9C8C: the original integrator clears the small residual
            // velocity and arms the following downward-state handler.
            player_.terrain_vertical_stop = 0xFF;
            player_.vy = 0;
        }
    } else if (player_.vy > 0x3B) {
        // The ROM's positive branch reloads D0 after the vertical-state
        // handoff. The native handoff below contributes 0x0078 before the
        // next state is published, so the exposed position step uses that
        // post-handoff velocity (the ascent branch already matches the direct
        // pre-step form above).
        const auto next_velocity = static_cast<std::int16_t>(player_.vy + 0x0078);
        player_.y += static_cast<std::int8_t>(fixed_high_byte(next_velocity));
        player_.vy = static_cast<std::int16_t>(player_.vy - 0x3C);
    } else {
        player_.terrain_vertical_stop = 0xFF;
        player_.vy = 0;
    }
}

void Engine::resolve_terrain(int previous_world_y) {
    const Level::TerrainQuery query = level_.query_player(player_world_x(), player_world_y());
    const Level::TerrainCell previous_down_probe =
        level_.resolve_player_cell(player_world_x(), previous_world_y + 8);
    const Level::TerrainCell* cell = &query.resolver;
    if (player_.vy >= 0 && !query.has_resolver_handler()
        && previous_down_probe.valid && previous_down_probe.behavior != 0
        && (previous_down_probe.handler != kTerrainNoOpHandler
            || previous_down_probe.behavior == 0x11)) {
        // Terrain_ResolvePlayerCell probes the row immediately below the
        // player on the descending path. This catches the floor crossing
        // before PLAYER_Y reaches the floor row itself, matching the ROM's
        // landing snap at the first frame over the surface.
        cell = &previous_down_probe;
    }
    player_.terrain_behavior = cell->valid ? cell->behavior : 0;
    player_.terrain_horizontal_response = 0;
    // Terrain_ResolvePlayerCell clears these scratch fields before it looks
    // up the selected behavior. FFF0C2 is deliberately cleared only when
    // the resolved behavior is not 0x47; the 0x47 handler uses it as a
    // one-shot latch on consecutive frames.
    player_.terrain_query_state_a = 0;
    player_.terrain_query_state_b = 0;
    player_.terrain_response_latch = 0;
    player_.terrain_state = 0;
    if (player_.terrain_behavior != 0x47) {
        player_.terrain_surface_latch = 0;
    }

    // The original resolver dispatches the behavior selected by the exact
    // cell. A zero behavior returns without changing motion; it does not
    // search forward for a nearby floor row and it does not add gravity.
    const bool has_handler = cell->valid && cell->behavior != 0
        && (cell->handler != kTerrainNoOpHandler || cell->behavior == 0x11);
    if (!has_handler) {
        // A zero-behavior cell is a no-op for the current terrain state. In
        // particular, the ROM does not turn a grounded player airborne merely
        // because the resolved row has no handler; the landing flag remains
        // latched until an actual vertical transition changes it.
        return;
    }

    // Only the ordinary surface handlers observed in the opening room
    // perform the floor snap. Special cells (for example 0x2B and 0x47)
    // dispatch their own response without converting a falling player into
    // a grounded player; the ROM handler at 0x001B5502 notably leaves VY
    // untouched.
    const bool floor_behavior = cell->behavior == 0x0A || cell->behavior == 0x11;
    if (floor_behavior && player_.vy >= 0) {
        player_.y = cell->row * 16 + kTerrainVisualOffsetY - camera_.y;
        player_.vy = 0;
        if (!player_.grounded || player_.vx == 0) {
            player_.vx = 0;
        }
        player_.grounded = true;
        player_.terrain_landing_state = 1;
        player_.terrain_vertical_stop = 0;
        player_.terrain_response_timer_state = 0;
    } else if (player_.vy < 0 && query.up.valid && query.up.behavior != 0
               && (query.up.handler != kTerrainNoOpHandler || query.up.behavior == 0x11)) {
        player_.terrain_stop_upward_motion = 0xFF;
    }

    // The ROM dispatches the handler on every resolver pass. Individual
    // handlers carry their own guards/latches; dispatching only on behavior
    // transitions loses those semantics for special terrain cells.
    apply_terrain_behavior(*cell);
}

void Engine::apply_terrain_behavior(const Level::TerrainCell& cell) {
    switch (cell.behavior) {
    case 0x20:  // TerrainHandler_SetTerminalCollision (0x001B5318)
        player_.terrain_terminal_transition = 0xFF;
        break;
    case 0x28:  // TerrainHandler_StopAndAlign (0x001B55E8)
        player_.vx = 0;
        player_.vy = 0;
        player_.terrain_horizontal_response = 0;
        player_.terrain_response_active = 0;
        player_.grounded = true;
        player_.x = ((visual_x() & ~0x0F) - camera_.x) - 4;
        player_.y = ((player_world_y() & ~0x0F) - camera_.y) + 2;
        break;
    case 0x29:  // TerrainHandler_LaunchPlayerBlock (0x001B557E)
        player_.vx = static_cast<std::int16_t>(-0x400);
        player_.vy = static_cast<std::int16_t>(-0x500);
        player_.grounded = false;
        player_.terrain_response_active = 0xFF;
        break;
    case 0x2B:  // TerrainHandler_StopAndAlignPlayer (0x001B5502)
        player_.vx = 0;
        player_.terrain_response_active = 0;
        player_.terrain_response_timer_state = 0;
        player_.x = ((visual_x() & ~0x0F) - camera_.x) + 6;
        break;
    case 0x2D:  // TerrainHandler_BouncePlayerBlock (0x001B56B6)
        player_.vx = static_cast<std::int16_t>(-0x400);
        player_.vy = static_cast<std::int16_t>(0x200);
        player_.grounded = false;
        break;
    case 0x30:  // TerrainHandler_LandingResponseBlock (0x001B537A)
        player_.vy = static_cast<std::int16_t>(player_.vy - 0x7C);
        player_.terrain_horizontal_response = 0;
        player_.terrain_landing_state = 0xFF;
        player_.grounded = player_.vy >= 0;
        break;
    case 0x40:  // TerrainHandler_MovePlayerRight (0x001B536C)
        player_.x += 8;
        player_.terrain_horizontal_response = 0;
        break;
    case 0x41:  // TerrainHandler_HorizontalResponseBlock (0x001B53A2)
        player_.x = std::max(0x14, player_.x - 8);
        player_.terrain_horizontal_response = 0;
        break;
    case 0x47:  // TerrainHandler_ToggleSurfaceMode (0x001B5470)
        if (player_.terrain_surface_latch == 0) {
            player_.terrain_surface_latch = 0xFF;
            player_.terrain_surface_mode ^= 1;
        }
        break;
    default:
        break;
    }
}

bool Engine::terrain_side_blocked(int direction) const {
    const int next_world_x = player_world_x() + direction * 3;
    const Level::TerrainQuery query = level_.query_player(next_world_x, player_world_y());
    return direction < 0 ? query.side_blocks_left() : query.side_blocks_right();
}

void Engine::apply_ground_movement(const InputState& input) {
    const int direction = (input.right ? 1 : 0) - (input.left ? 1 : 0);
    if (direction == 0) {
        return;
    }

    // The recovered ground response path uses a three-pixel movement step
    // (DAT_FFF0B0=3) and leaves PLAYER_VX clear. This is distinct from the
    // fixed-point velocity used for airborne motion and terrain launches.
    if (!terrain_side_blocked(direction)) {
        player_.x = std::clamp(player_.x + direction * 3, 0x14, 0x130);
    }
    player_.vx = 0;
}

void Engine::initialize_camera_alignment() {
    camera_.pixel_x = (camera_.x & 0x0F) + player_.x;
    camera_.pixel_y = (camera_.y & 0x0F) + player_.y;
    camera_.tile_x = camera_.x & ~0x0F;
    camera_.tile_y = camera_.y & ~0x0F;
}

bool Engine::rebase_camera_reference() {
    bool reference_rebased = false;
    if (camera_.scroll_left_pending || camera_.scroll_right_pending) {
        if (camera_.scroll_x >= 0x10) {
            camera_.scroll_x -= 0x10;
            camera_.reference_x += 0x10;
            camera_.scroll_left_pending = false;
            camera_.scroll_right_pending = false;
            reference_rebased = true;
        } else if (camera_.scroll_x < -0x0F) {
            camera_.scroll_x += 0x10;
            camera_.reference_x -= 0x10;
            camera_.scroll_left_pending = false;
            camera_.scroll_right_pending = false;
            reference_rebased = true;
        }
    }
    if (camera_.scroll_up_pending || camera_.scroll_down_pending) {
        if (camera_.scroll_y >= 0x10) {
            camera_.scroll_y -= 0x10;
            camera_.reference_y += 0x10;
            camera_.scroll_up_pending = false;
            camera_.scroll_down_pending = false;
            reference_rebased = true;
        } else if (camera_.scroll_y < -0x0F) {
            camera_.scroll_y += 0x10;
            camera_.reference_y -= 0x10;
            camera_.scroll_up_pending = false;
            camera_.scroll_down_pending = false;
            reference_rebased = true;
        }
    }
    // The ROM dispatcher consumes the pending byte every frame. A reference
    // rebase is conditional on the accumulator crossing 16 pixels, but the
    // pending marker itself is not sticky.
    camera_.scroll_left_pending = false;
    camera_.scroll_right_pending = false;
    camera_.scroll_up_pending = false;
    camera_.scroll_down_pending = false;
    return reference_rebased;
}

void Engine::update_camera() {
    // 0x001AA8FA delays the follow pass after a player mode/threshold change.
    // The delay is observable in the jump trace: the camera remains still for
    // seven frames after the jump threshold is installed.
    if (camera_.update_delay > 0) {
        --camera_.update_delay;
        return;
    }
    if (camera_.special_mode != 0 || camera_.scene_state == 8) {
        return;
    }

    const auto horizontal_delta = [&]() {
        const int difference = player_.x - camera_.horizontal_threshold;
        if (difference == 0) {
            return;
        }
        const int magnitude = std::abs(difference);
        const int index = std::min(magnitude, static_cast<int>(kCameraHorizontalDampening.size() - 1));
        const int delta = kCameraHorizontalDampening[static_cast<std::size_t>(index)];
        if (delta == 0) {
            return;
        }
        if (difference < 0) {
            if (camera_.reference_x < 0x11) {
                return;
            }
            player_.x += delta;
            camera_.x -= delta;
            camera_.scroll_x -= delta;
            camera_.scroll_left_pending = true;
            return;
        }
        const int effective = camera_.reference_x + camera_.scroll_x + delta;
        if (effective >= camera_.level_width - 0x161) {
            return;
        }
        player_.x -= delta;
        camera_.x += delta;
        camera_.scroll_x += delta;
        camera_.scroll_right_pending = true;
    };

    const auto vertical_delta = [&]() {
        const int difference = player_.y - camera_.vertical_threshold;
        if (difference == 0) {
            return;
        }
        const int magnitude = std::abs(difference);
        // The vertical table is immediately followed by the level table in
        // the ROM at 0x2C78. Valid camera errors stop at its final byte.
        const int index = std::min(magnitude, 0xD3);
        const int delta = kCameraVerticalDampening[static_cast<std::size_t>(index)];
        if (delta == 0) {
            return;
        }
        if (difference < 0) {
            if (camera_.reference_y < 0x11) {
                return;
            }
            player_.y += delta;
            camera_.y -= delta;
            camera_.scroll_y -= delta;
            camera_.scroll_up_pending = true;
            return;
        }
        const int effective = camera_.reference_y + camera_.scroll_y + delta;
        if (effective >= camera_.level_height - 0xF1) {
            return;
        }
        player_.y -= delta;
        camera_.y += delta;
        camera_.scroll_y += delta;
        camera_.scroll_down_pending = true;
    };

    horizontal_delta();
    vertical_delta();

}

void Engine::update_state08(const InputState& input) {
    // State 08 enters the transition branch at 0x001A9D18 and bypasses the
    // normal physics/camera follower. The controller fallback at 0x001A9C9A
    // moves the local coordinates in eight-pixel steps within these bounds.
    if (input.right && player_.x < 0x130) {
        player_.x += 8;
    }
    if (input.left && player_.x >= 0x10) {
        player_.x -= 8;
    }
    if (input.up && player_.y >= 0x10) {
        player_.y -= 8;
    }
    if (input.down && player_.y < 0x1E0) {
        player_.y += 8;
    }
    player_.grounded = false;
}

void Engine::update(const InputState& input) {
    if (camera_.scene_state == 8) {
        update_state08(input);
        animation_.update(
            SpritePose::Idle,
            input.left && !input.right,
            AnimationContext{
                player_.x,
                player_.y,
                player_world_x(),
                player_world_y(),
                player_.vx,
                player_.vy,
                player_.grounded,
                player_.terrain_response_timer_state,
            }
        );
        ++frame_;
        return;
    }
    update_terrain_input(input);
    const bool was_grounded = player_.grounded;
    const int input_direction = (input.right ? 1 : 0) - (input.left ? 1 : 0);
    const bool ground_release = was_grounded && !input.jump_pressed && input_direction == 0
        && last_ground_direction_ != 0 && player_.vx == 0;
    const bool vertical_stop_before_frame = player_.terrain_vertical_stop != 0;
    const bool start_jump = input.jump_pressed && was_grounded;
    const AnimationContext animation_context{
        player_.x,
        player_.y,
        player_world_x(),
        player_world_y(),
        player_.vx,
        player_.vy,
        was_grounded,
        player_.terrain_response_timer_state,
    };

    if (was_grounded && player_.grounded) {
        if (input.left != input.right) {
            const int threshold = input.left ? 0xF0 : 0x70;
            if (camera_.horizontal_threshold != threshold) {
                camera_.horizontal_threshold = threshold;
                camera_.update_delay = 7;
            }
        }
        apply_ground_movement(input);
    } else if (input.left && !input.right) {
        player_.vx = player_.vx >= 0 ? static_cast<std::int16_t>(-0x300)
                                     : std::max<std::int16_t>(player_.vx, -0x300);
    } else if (input.right && !input.left) {
        player_.vx = player_.vx <= 0 ? static_cast<std::int16_t>(0x300)
                                     : std::min<std::int16_t>(player_.vx, 0x300);
    }

    const int previous_world_y = player_world_y();
    integrate_motion();
    resolve_terrain(previous_world_y);
    if (!player_.grounded && player_.terrain_behavior == 0
        && (vertical_stop_before_frame || player_.terrain_response_timer_state != 0)) {
        // This is the post-integrator jump/vertical-state handoff. It is
        // intentionally after resolve_terrain: the original frame where the
        // residual upward velocity is cleared still exposes VY=0; the next
        // frame starts the positive phase at 0x003C.
        if (player_.terrain_response_timer_state == 0) {
            player_.vy = 0x003C;
            player_.terrain_response_timer_state = 1;
        } else if (player_.vy < 0x800) {
            player_.vy = static_cast<std::int16_t>(player_.vy + 0x0078);
        }
        player_.terrain_vertical_stop = 0;
    }
    if (start_jump && player_.grounded) {
        // The recovered frame order applies the jump handler after motion and
        // terrain resolution (Player_Update -> Terrain_Resolve -> jump
        // handler). This leaves the impulse visible for the next frame before
        // the integrator consumes it.
        player_.vy = static_cast<std::int16_t>(-0x200);
        player_.grounded = false;
        player_.terrain_response_active = 0xFF;
        player_.terrain_response_timer_state = 0;
        player_.terrain_vertical_stop = 0;
        player_.terrain_landing_state = 0xFF;
        camera_.horizontal_threshold = 0xB0;
        camera_.vertical_threshold = 0x170;
        camera_.update_delay = 7;
    }
    // The ROM updates the follow camera before the tile-update dispatcher
    // consumes a pending 16-pixel reference shift. Keeping the rebase at the
    // end of the frame makes the externally visible state match that order:
    // local movement, damped camera movement, then reference/scroll repair.
    update_camera();
    rebase_camera_reference();
    if (ground_release) {
        // The first no-input frame enters the ROM's inertial ground path after
        // the position/camera work. The exposed velocity is 0x038C (or its
        // signed mirror), then the normal integrator decays it by 0x28.
        player_.vx = static_cast<std::int16_t>(last_ground_direction_ * 0x038C);
        last_ground_direction_ = 0;
    }
    if (was_grounded && player_.grounded && input_direction != 0) {
        last_ground_direction_ = input_direction;
    } else if (!player_.grounded) {
        last_ground_direction_ = 0;
    }
    const SpritePose desired_pose = !player_.grounded
        ? SpritePose::Jump
        : (!was_grounded
            ? SpritePose::Landing
            : (animation_.pose() == SpritePose::Landing && !animation_.finished()
                ? SpritePose::Landing
                : (input.left != input.right
                    ? SpritePose::Run
                    : (animation_.pose() == SpritePose::Run || animation_.pose() == SpritePose::Brake
                        ? SpritePose::Brake : SpritePose::Idle))));
    // The original actor VM runs before the player movement update reaches
    // the stable frame boundary. Feed it the pre-integration state so its
    // F4/F2 conditions see the same velocity and landing fields as MAME.
    animation_.update(
        desired_pose,
        input.left && !input.right,
        animation_context
    );
    // Movement's stop transition (the zero-velocity branch at 0x001AE4F8)
    // replaces the brake stream with the short deceleration stream. It is a
    // gameplay-state write to FF7E60, not an F8 bytecode operation, so apply
    // it after the actor VM tick just as the original frame does.
    if (animation_.rom_loaded()
        && desired_pose == SpritePose::Brake
        && animation_.pose() == SpritePose::Brake
        && player_.vx == 0
        && animation_.stream_entry() != 0x001226CE) {
        animation_.select_stream_entry(0x001226CE);
    }
    ++frame_;
}

void Engine::write_state(std::ostream& output, const std::string& input_token) const {
    // Keep this stream deliberately aligned with re/mame/lua/main.lua. It is
    // intentionally a small, valid subset of the shared schema: scene and
    // actor emulation do not exist in the vertical slice yet, but the player
    // and camera fields mirror the fixed-ROM coordinate model.
    // The ROM keeps its landing flag asserted for the launch-impulse frame,
    // even though the gameplay handler has already left its internal grounded
    // path. Mirror that externally visible state in the shared trace without
    // changing the native physics predicate used by update().
    const bool trace_grounded = player_.grounded
        || player_.vy == static_cast<std::int16_t>(-0x200);
    int state_sprite_frame = animation_.sprite_frame();
    if (state_sprite_frame < 0 || animation_.rom_loaded()) {
        state_sprite_frame = sprites_.frame_index_for_address(
            static_cast<int>(animation_.frame_pointer())
        );
    }
    if (state_sprite_frame < 0) state_sprite_frame = SpriteDatabase::kIdleFrame;

    output << "{\"type\":\"state\",\"format\":\"openaladdin-frame-state-v1\""
           << ",\"frame\":" << frame_
           << ",\"input\":\"" << input_token << "\""
           << ",\"player\":{\"x\":" << player_.x
           << ",\"y\":" << player_.y
           << ",\"world_x\":" << player_world_x()
           << ",\"world_y\":" << player_world_y()
           << ",\"vx\":" << player_.vx
           << ",\"vy\":" << player_.vy
           << ",\"animation_pc\":" << animation_.animation_pc()
           << ",\"animation_state\":\""
           << (animation_.pose() == SpritePose::Idle ? "idle"
               : animation_.pose() == SpritePose::Run ? "run"
               : animation_.pose() == SpritePose::Brake ? "brake"
               : animation_.pose() == SpritePose::Jump ? "jump" : "landing")
           << "\""
           << ",\"animation_timer\":" << animation_.timer()
           << ",\"animation_stream_entry\":" << animation_.stream_entry()
           << ",\"sprite_frame\":" << state_sprite_frame
           << ",\"frame_ptr\":" << (animation_.rom_loaded()
               ? animation_.frame_pointer()
               : sprites_.frame(animation_.sprite_frame()).address)
           << ",\"facing_left\":" << (animation_.facing_left() ? "true" : "false")
           << ",\"grounded\":" << (trace_grounded ? "true" : "false") << "}"
           << ",\"scene\":{\"state\":" << camera_.scene_state
           << ",\"script_cursor\":0"
           << ",\"script_data_cursor\":0"
           << ",\"table_index\":0"
           << ",\"script_pending\":0"
           << ",\"vdp_update\":0"
           << ",\"vdp_clear\":0"
           << ",\"transition_event\":0"
           << ",\"script_countdown\":0"
           << ",\"script_gate\":0"
           << ",\"player_gate\":0"
           << ",\"player_lock\":0"
           << ",\"player_countdown\":0"
           << ",\"player_terminal\":" << static_cast<unsigned>(player_.terrain_terminal_transition) << "}"
           << ",\"camera\":{\"x\":" << camera_.x
           << ",\"y\":" << camera_.y
           << ",\"reference_x\":" << camera_.reference_x
           << ",\"reference_y\":" << camera_.reference_y
           << ",\"horizontal_threshold\":" << camera_.horizontal_threshold
           << ",\"vertical_threshold\":" << camera_.vertical_threshold
           << ",\"scroll_x\":" << camera_.scroll_x
           << ",\"scroll_y\":" << camera_.scroll_y
           << ",\"pixel_x\":" << camera_.pixel_x
           << ",\"pixel_y\":" << camera_.pixel_y
           << ",\"tile_x\":" << camera_.tile_x
           << ",\"tile_y\":" << camera_.tile_y
           << ",\"level_width\":" << camera_.level_width
           << ",\"level_height\":" << camera_.level_height
           << ",\"update_delay\":" << camera_.update_delay
           << ",\"special_mode\":" << camera_.special_mode
           << ",\"state_08\":" << (camera_.scene_state == 8 ? "true" : "false")
           << ",\"scroll_left_pending\":" << (camera_.scroll_left_pending ? 255 : 0)
           << ",\"scroll_right_pending\":" << (camera_.scroll_right_pending ? 255 : 0)
           << ",\"scroll_up_pending\":" << (camera_.scroll_up_pending ? 255 : 0)
           << ",\"scroll_down_pending\":" << (camera_.scroll_down_pending ? 255 : 0) << "}"
           << ",\"terrain\":{\"world_x\":" << visual_x()
           << ",\"world_y\":" << player_world_y()
           << ",\"query_result\":" << static_cast<unsigned>(player_.terrain_query_result)
           << ",\"push_right\":" << static_cast<unsigned>(player_.terrain_push_right)
           << ",\"push_left\":" << static_cast<unsigned>(player_.terrain_push_left)
           << ",\"push_up\":" << static_cast<unsigned>(player_.terrain_push_up)
           << ",\"push_down\":" << static_cast<unsigned>(player_.terrain_push_down)
           << ",\"behavior\":" << static_cast<unsigned>(player_.terrain_behavior)
           << ",\"horizontal_response\":" << player_.terrain_horizontal_response
           << ",\"response_active\":" << static_cast<unsigned>(player_.terrain_response_active)
           << ",\"vertical_stop\":" << static_cast<unsigned>(player_.terrain_vertical_stop)
           << ",\"landing_state\":" << static_cast<unsigned>(player_.terrain_landing_state)
           << ",\"surface_mode\":" << static_cast<unsigned>(player_.terrain_surface_mode)
           << ",\"surface_latch\":" << static_cast<unsigned>(player_.terrain_surface_latch)
           << ",\"stop_left_motion\":" << static_cast<unsigned>(player_.terrain_stop_left_motion)
           << ",\"stop_right_motion\":" << static_cast<unsigned>(player_.terrain_stop_right_motion)
           << ",\"stop_upward_motion\":" << static_cast<unsigned>(player_.terrain_stop_upward_motion)
           << ",\"response_timer_state\":" << static_cast<unsigned>(player_.terrain_response_timer_state)
           << ",\"query_state_a\":" << static_cast<unsigned>(player_.terrain_query_state_a)
           << ",\"query_state_b\":" << static_cast<unsigned>(player_.terrain_query_state_b)
           << ",\"state\":" << static_cast<unsigned>(player_.terrain_state)
           << ",\"response_latch\":" << static_cast<unsigned>(player_.terrain_response_latch) << "}"
           << ",\"actors\":[]}\n";
}

void Engine::render(SDL_Renderer* renderer) {
    if (framebuffer_.size() != static_cast<std::size_t>(kScreenWidth * kScreenHeight)) {
        return;
    }
    // The native renderer consumes the same WORLD_CAMERA origin as terrain
    // and actors. The old player-centered calculation made rendering drift
    // independently from Genesis gameplay coordinates.
    camera_render_x_ = std::clamp(camera_.x, 0, std::max(0, level_.background_width() - kScreenWidth));
    camera_render_y_ = std::clamp(camera_.y, 0, std::max(0, level_.background_height() - kScreenHeight));

    const auto& background = level_.background_rgba();
    for (int y = 0; y < kScreenHeight; ++y) {
        for (int x = 0; x < kScreenWidth; ++x) {
            const int source_x = camera_render_x_ + x;
            const int source_y = camera_render_y_ + y;
            std::uint32_t pixel = rgba(10, 10, 18);
            if (source_x >= 0 && source_x < level_.background_width() &&
                source_y >= 0 && source_y < level_.background_height()) {
                const std::size_t source = static_cast<std::size_t>((source_y * level_.background_width() + source_x) * 4);
                pixel = rgba(background[source], background[source + 1], background[source + 2]);
            }
            framebuffer_[static_cast<std::size_t>(y * kScreenWidth + x)] = pixel;
        }
    }

    int player_frame_index = animation_.sprite_frame();
    if (player_frame_index < 0 || animation_.rom_loaded()) {
        player_frame_index = sprites_.frame_index_for_address(
            static_cast<int>(animation_.frame_pointer())
        );
    }
    if (player_frame_index < 0) {
        player_frame_index = SpriteDatabase::kIdleFrame;
    }
    const SpriteFrame& player_frame = sprites_.frame(player_frame_index);
    SpriteRenderer::draw(
        player_frame,
        sprites_.palette(),
        framebuffer_,
        kScreenWidth,
        kScreenHeight,
        visual_x() - camera_render_x_,
        visual_y() - camera_render_y_,
        animation_.facing_left(),
        false,
        SpriteDatabase::kPlayerPaletteLine
    );

    if (texture_ == nullptr) {
        // RGBA32 means RGBA byte order on the host. RGBA8888 is a packed
        // numeric format whose byte order is different on little-endian
        // systems, producing the channel-swapped pink output seen in the
        // first window test.
        texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kScreenWidth, kScreenHeight);
        if (texture_ == nullptr) {
            throw std::runtime_error(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
        }
    }
    if (SDL_UpdateTexture(texture_, nullptr, framebuffer_.data(), kScreenWidth * static_cast<int>(sizeof(std::uint32_t))) != 0) {
        throw std::runtime_error(std::string("SDL_UpdateTexture failed: ") + SDL_GetError());
    }
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

}  // namespace openaladdin
